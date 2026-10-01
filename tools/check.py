#!/usr/bin/env python3
"""Framework checks. Bounded JSON Schema subset, not a general validator."""
from pathlib import Path
from urllib.parse import urlsplit
import json
import os
import re
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


def validate(value, schema, location='$'):
    """Validate exactly the vocabulary used by the two standalone schemas."""
    known = {'$schema','$id','title','description','type','const','enum','properties','required','additionalProperties','items','minItems','maxItems','minLength','maxLength','pattern','minimum','maximum','writeOnly'}
    unknown = set(schema) - known
    if unknown:
        raise ValueError(f'Unsupported schema keywords: {sorted(unknown)}')
    def fail(message):
        raise ValueError(f'{location}: {message}')
    types = {'object': lambda x: type(x) is dict, 'array': lambda x: type(x) is list,
             'string': lambda x: type(x) is str, 'integer': lambda x: type(x) is int,
             'boolean': lambda x: type(x) is bool, 'null': lambda x: x is None}
    if 'type' in schema:
        wanted = schema['type'] if isinstance(schema['type'], list) else [schema['type']]
        if not any(types[t](value) for t in wanted): fail(f'expected {wanted}')
    if 'const' in schema and (type(value) is not type(schema['const']) or value != schema['const']): fail('const mismatch')
    if 'enum' in schema and not any(type(value) is type(x) and value == x for x in schema['enum']): fail('enum mismatch')
    if type(value) is dict:
        for key in schema.get('required', []):
            if key not in value: fail(f'missing {key}')
        props = schema.get('properties', {})
        for key, child in value.items():
            if key in props: validate(child, props[key], f'{location}.{key}')
            elif schema.get('additionalProperties') is False: fail(f'unexpected {key}')
    if type(value) is list:
        if len(value) < schema.get('minItems', 0) or len(value) > schema.get('maxItems', float('inf')): fail('array length')
        for i, child in enumerate(value): validate(child, schema.get('items', {}), f'{location}[{i}]')
    if type(value) is str:
        if len(value) < schema.get('minLength',0) or len(value) > schema.get('maxLength',float('inf')): fail('text length')
        if 'pattern' in schema and re.search(schema['pattern'], value) is None: fail('pattern mismatch')
    if type(value) is int:
        if value < schema.get('minimum', -float('inf')) or value > schema.get('maximum', float('inf')): fail('number range')


def read_json(relative):
    return json.loads((ROOT / relative).read_text())


def source_files(suffix):
    """Check our sources, not generated ESP-IDF output or downloaded dependencies."""
    excluded = {'.git', 'build', 'managed_components', '__pycache__', '.venv'}
    for directory, children, names in os.walk(ROOT):
        children[:] = [name for name in children if name not in excluded]
        for name in names:
            if name.endswith(suffix):
                yield Path(directory) / name


def validate_config(data):
    validate(data, read_json('contracts/config.schema.json'))
    for group in ['favorites', 'stations']:
        ids = [x['id'] for x in data['catalog'][group]]
        if len(set(ids)) != len(ids): raise ValueError('duplicate catalog ID')
    for item in data['catalog']['favorites']:
        if item['kind'].removeprefix('spotify_') != item['spotify_uri'].split(':')[1]:
            raise ValueError('Spotify kind/URI mismatch')
    for item in data['catalog']['stations']:
        for field in ['url', 'resolved_url']:
            parsed = urlsplit(item[field])
            if not parsed.hostname or parsed.username or parsed.password:
                raise ValueError('invalid radio URL or credentials embedded')
    # This is config validation only. DNS/redirect/SSRF validation at network time
    # remains mandatory; a parse result does not authorize a network request.


def validate_manifest(data, *, installable=False):
    validate(data, read_json('contracts/ota-manifest.schema.json'))
    images = data['images']
    if {x['role'] for x in images} != {'companion_esp32', 'controller_s3'}:
        raise ValueError('exactly one image per processor role required')
    for image in images:
        if image['version'] != data['version'] or image['release_id'] != data['release_id']:
            raise ValueError('release identity mismatch')
        for prefix in ['accepted_peer_protocol', 'readable_config_schema']:
            lo, hi = image[prefix+'_min'], image[prefix+'_max']
            if lo > hi or hi-lo >= 8: raise ValueError('invalid or unbounded compatibility range')
        if urlsplit(image['url']).scheme != 'https': raise ValueError('OTA requires HTTPS')
    if installable:
        # No caller can obtain positive installation authorization here: this
        # structural checker has no cryptographic verifier or actual hardware.
        raise ValueError('installation requires production cryptographic/hardware verification; not implemented')


def check_links():
    for path in source_files('.md'):
        for target in re.findall(r'\[[^\]]*\]\(([^)\s]+)\)', path.read_text()):
            if target.startswith(('http:', 'https:', '#', '/')): continue
            local = (path.parent / target.split('#')[0]).resolve()
            if not local.exists(): raise ValueError(f'Broken link {path.relative_to(ROOT)} -> {target}')


def check_openapi():
    api = read_json('contracts/openapi.json')
    ids = []
    def visit(node):
        if isinstance(node, dict):
            if '$ref' in node:
                ref = node['$ref']
                if not ref.startswith('#/'): raise ValueError('external API ref not checked')
                cur = api
                for part in ref[2:].split('/'): cur = cur[part]
            for val in node.values(): visit(val)
        elif isinstance(node, list):
            for val in node: visit(val)
    visit(api)
    for path, methods in api['paths'].items():
        if not path.startswith('/api/v1/'): raise ValueError('unversioned API path')
        for op in methods.values(): ids.append(op['operationId'])
    if len(ids) != len(set(ids)): raise ValueError('duplicate operation IDs')


def main():
    for file in source_files('.json'): json.loads(file.read_text())
    validate_config(read_json('examples/config.example.json'))
    validate_manifest(read_json('examples/update-manifest.example.json'))
    check_links()
    check_openapi()
    print('JSON examples, semantic contracts, API refs and document links: OK', flush=True)
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.discover(str(ROOT/'tests')))
    if not result.wasSuccessful(): return 1
    compiler = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
    if compiler:
        subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT/'firmware/include'),'-x','c++','-fsyntax-only','-'], input='#include "pw_framework.hpp"\n',text=True,check=True)
        print('C++17 interface syntax: OK', flush=True)
    else:
        print('C++ compiler missing: syntax check NOT RUN', flush=True)
    node = shutil.which('node')
    if node:
        subprocess.run([node,'--check',str(ROOT/'web/app.js')],check=True)
        subprocess.run([node,'--check',str(ROOT/'firmware/web/app.js')],check=True)
        subprocess.run([node,'--check',str(ROOT/'tools/spotify_setup/static/app.js')],check=True)
        print('Web JavaScript syntax: OK', flush=True)
    else:
        print('Node missing: JavaScript syntax check NOT RUN', flush=True)
    print('No device/Spotify/crypto/visual-browser acceptance claimed.',flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())
