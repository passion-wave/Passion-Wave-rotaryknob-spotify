#!/usr/bin/env python3
"""Reproduce the compact offline German postcode/place index from a GeoNames ZIP."""
from __future__ import annotations
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, default=Path('firmware/web/places-de.json.gz'))
    parser.add_argument('--date', required=True, help='Source retrieval date YYYY-MM-DD')
    args = parser.parse_args()
    if not re.fullmatch(r'\d{4}-\d{2}-\d{2}', args.date):
        parser.error('Invalid retrieval date')
    with zipfile.ZipFile(args.source) as source:
        entry = source.getinfo('DE.txt')
        if entry.file_size > 16 * 1024 * 1024:
            parser.error('Source exceeds the bounded importer size')
        rows = set()
        for line in source.read(entry).decode('utf-8').splitlines():
            fields = line.split('\t')
            if len(fields) != 12 or fields[0] != 'DE':
                parser.error('Unexpected GeoNames row format')
            code, name, state = fields[1], fields[2], fields[3]
            if not re.fullmatch(r'\d{5}', code) or not name or len(name) > 180:
                parser.error('Invalid postcode/place name')
            lat, lon = round(float(fields[9]), 4), round(float(fields[10]), 4)
            if not (47 <= lat <= 56 and 5 <= lon <= 16):
                parser.error('Coordinate outside German index bounds')
            rows.add((code, name, state, lat, lon))
    payload = {'columns': ['postcode', 'name', 'state', 'latitude', 'longitude'],
               'places': sorted(rows)}
    raw = json.dumps(payload, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    packed = gzip.compress(raw, compresslevel=9, mtime=0)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(packed)
    evidence = {'source': 'https://download.geonames.org/export/zip/DE.zip',
                'license': 'CC-BY-4.0', 'attribution': 'GeoNames', 'retrieved': args.date,
                'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
                'output_sha256': hashlib.sha256(packed).hexdigest(),
                'records': len(rows), 'json_bytes': len(raw), 'gzip_bytes': len(packed),
                'changes': 'German records, five fields, coordinates rounded to four decimals; duplicates removed and sorted; gzip.'}
    args.output.with_name('places-de.provenance.json').write_text(
        json.dumps(evidence, ensure_ascii=False, indent=2) + '\n')
    print(f'{len(rows)} offline places; {len(raw)} JSON bytes / {len(packed)} gzip bytes')


if __name__ == '__main__':
    main()
