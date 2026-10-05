#!/usr/bin/env python3
"""Chromium/WebKit mobile regression against real UI, simulated private API only."""
import argparse, copy, json, time, tempfile
from pathlib import Path
from urllib.parse import urlsplit, parse_qs
from playwright.sync_api import sync_playwright
from device_web import BASE, WEB

def run(engine, width, playwright, screenshots):
    state = copy.deepcopy(BASE)
    state['capabilities']['spotify'] = True
    spotify = dict(enabled=True, linked=True, state='ready', error='none', session=23,
                   selection_generation=1, selected={}, playback={}, actions={})
    jobs, reads, writes, errors = {}, [], [], []
    mode = {'forbidden': False, 'hold': True}
    def route(r):
        req=r.request; u=urlsplit(req.url); path=u.path
        if u.hostname!='192.0.2.1': raise AssertionError('Unexpected external request')
        if path in ('/','/app.js','/style.css'):
            name='index.html' if path=='/' else path[1:]
            r.fulfill(body=(WEB/name).read_bytes(),content_type={'index.html':'text/html','app.js':'text/javascript','style.css':'text/css'}[name]);return
        reads.append(path)
        result={};status=200
        if path=='/api/v1/session': result=dict(secure_write=state['secure_write'],write_allowed=state.get('write_allowed',state['secure_write']),unprotected_lab=state.get('unprotected_lab',False),csrf='fixture')
        elif path=='/api/v1/session/pair':
            assert req.post_data_json=={'code':'123456'}
            assert req.headers.get('origin')=='http://192.0.2.1'
            state.update(copy.deepcopy(BASE));state['secure_write']=False;state['write_allowed']=True;state['unprotected_lab']=True;state['capabilities'].update(spotify=True,lab_lan_http=True)
            result={'paired':True}
        elif path=='/api/v1/status': result=state
        elif path=='/api/v1/spotify/snapshot': result=spotify
        elif path=='/api/v1/spotify/devices': result=dict(session=23,devices=[])
        elif path=='/api/v1/spotify/library' and req.method=='POST':
            assert req.headers['x-csrf-token']=='fixture'
            job=req.post_data_json; assert job['session']==23
            jobs[job['kind']]=job; result={'accepted':True};status=202
        elif path=='/api/v1/spotify/library':
            kind=parse_qs(u.query)['kind'][0]; job=jobs[kind]; offset=job['offset']
            result=dict(state='loading' if mode['hold'] else 'forbidden' if mode['forbidden'] else 'ready',kind=kind,session=23,offset=offset,next_offset=offset+1,total=2,has_more=offset==0,items=[dict(name='<b>Private Liste</b>' if kind=='playlist' else 'Mein Podcast',uri=f'spotify:{kind}:{offset:022d}')])
        elif path=='/api/v1/catalog' and req.method=='PUT':
            assert req.headers['x-csrf-token']=='fixture'
            writes.append(req.post_data_json); state['catalog']=writes[-1];state['config_revision']+=1
            result={'config_revision':state['config_revision']}
        else: result={'state':'locked','upload_enabled':False}
        r.fulfill(status=status,body=json.dumps(result),content_type='application/json')
    browser=getattr(playwright,engine).launch();page=browser.new_page(viewport={'width':width,'height':844},is_mobile=width<700,has_touch=width<700)
    page.on('pageerror',lambda e:errors.append(str(e)));page.route('**/*',route)
    started=time.monotonic();page.goto('http://192.0.2.1/#content')
    page.locator('#favorite-link').wait_for();page.wait_for_function('!document.querySelector("#favorite-link").closest("fieldset").disabled')
    elapsed=time.monotonic()-started;assert elapsed<4,elapsed
    page.wait_for_function('document.querySelector("#library-status").textContent.includes("wird geladen")')
    # A slow Spotify job must not lock local inputs or navigation.
    started=time.monotonic();page.locator('#favorite-link').fill('https://open.spotify.com/playlist/2222222222222222222222')
    page.locator('#favorite-name').fill('Manueller Favorit');assert time.monotonic()-started<2
    assert page.locator('#favorite-link').input_value().startswith('https://')
    mode['hold']=False
    page.wait_for_function('document.querySelector("#library-choice").options.length===2')
    assert page.locator('#library-choice option').nth(1).inner_text()=='<b>Private Liste</b>'
    assert page.locator('#library-choice b').count()==0
    page.locator('#library-choice').select_option('spotify:playlist:0000000000000000000000')
    page.locator('#library-add').click();page.locator('#library-more').click()
    page.wait_for_function('document.querySelector("#library-choice").options.length===3')
    page.locator('#library-kind').select_option('show')
    page.wait_for_function('document.querySelector("#library-choice option:last-child").textContent==="Mein Podcast"')
    page.locator('#library-choice').select_option('spotify:show:0000000000000000000000');page.locator('#library-add').click()
    page.locator('#favorite-form button[type=submit]').click()
    assert page.locator('#favorites-list .catalog-row').count()==3
    page.locator('#save-catalog').click();page.wait_for_function('document.querySelector("#catalog-save-state").textContent.includes("ist gespeichert")')
    assert len(writes[-1]['favorites'])==3
    assert {x['kind'] for x in writes[-1]['favorites']}=={'spotify_playlist','spotify_show'}
    name=page.locator('#favorites-list .item-edit').first;name.focus()
    # A background status response with the same catalog must preserve the actual DOM node/focus.
    page.evaluate('window.focusedCatalogInput=document.activeElement')
    page.locator('#retry-connection').dispatch_event('click')
    page.wait_for_timeout(200)
    assert page.evaluate('document.activeElement===window.focusedCatalogInput && window.focusedCatalogInput.isConnected')
    mode['forbidden']=True;page.locator('#library-refresh').click()
    page.wait_for_function('document.querySelector("#library-status").textContent.includes("erneut")')
    assert page.locator('#favorite-link').is_enabled()
    assert page.locator('#favorites-list .catalog-row').count()==3
    assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
    for selector in ['#library-kind','#library-choice','#favorite-link']:
        assert page.locator(selector).evaluate('(e)=>parseFloat(getComputedStyle(e).fontSize)>=16')
        assert page.locator(selector).bounding_box()['height']>=44
    screenshots.mkdir(parents=True,exist_ok=True);page.screenshot(path=str(screenshots/f'{engine}-{width}-library.png'),full_page=True)
    # LAN privacy guard and inline explanation, not silently disabled fields.
    state['secure_write']=False
    for key in ('catalog','settings','weather'):state.pop(key,None)
    page.reload();page.locator('#content-access').wait_for(state='visible')
    assert 'Heim-WLAN' in page.locator('#content-access').inner_text()
    assert page.locator('#favorite-link').is_disabled()
    assert page.locator('#library-choice').is_disabled()
    state['capabilities']['lab_lan_http']=True
    page.reload();page.locator('#lan-pairing').wait_for(state='visible')
    page.locator('#lan-code').fill('123456');page.locator('#lan-pair-form button').click()
    page.wait_for_function('!document.querySelector("#favorite-link").closest("fieldset").disabled')
    assert page.locator('#lan-active').is_visible()
    assert page.locator('#lan-code').input_value()==''
    state['write_allowed']=False;state['unprotected_lab']=False
    page.locator('#retry-connection').dispatch_event('click')
    page.locator('#lan-pairing').wait_for(state='visible')
    assert page.locator('#favorite-link').is_disabled()
    assert not errors,errors
    browser.close();print(f'{engine} {width}px: library, pagination, consent error, manual links, focus, access, layout PASS; simulated startup {elapsed:.2f}s',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--screenshots',type=Path,default=Path(tempfile.gettempdir()) / 'pw-library-browser');args=parser.parse_args()
    with sync_playwright() as p:
        for engine in ('chromium','webkit'):
            for width in (360,390,1280):run(engine,width,p,args.screenshots)
