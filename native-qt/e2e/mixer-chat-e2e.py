"""Verify private Social Stream dock -> featured overlay -> native Program rendering.

Injects a synthetic incoming chat event into the real dock. Does not authenticate
to a social platform or send a chat message to anyone else.
"""
from pathlib import Path
exec(Path(__file__).with_name('mixer-desktop-e2e.py').read_text().split('class Fixture')[0])
import os,sys,uuid
sys.path.insert(0,str(Path('.cache/mixer-qa').resolve()))
from playwright.sync_api import sync_playwright
profile=Path(os.environ['LOCALAPPDATA'])/'VideoMixer/browser'
show=profile.parent/'studio.mixer.json'
session='mixerqa'+uuid.uuid4().hex[:20]
window.child_window(title='Studio',control_type='TabItem').click_input()
studio_mode()
scene('Break');press('takeScene')
remove_sources(('Social chat ',))
scene('Opening'); press('takeScene')
press('socialChat'); prompt=window.child_window(title='Social Stream Ninja',control_type='Window')
prompt.child_window(control_type='Edit').set_edit_text(session); prompt.child_window(title='OK',control_type='Button').invoke()
time.sleep(3); press('interactSource'); time.sleep(3)
data=json.loads(show.read_text()); source=next(s for s in data['sources'] if s['name']=='Social chat')
def page_for(pw,id,part):
    port=int((profile/id/'EBWebView/DevToolsActivePort').read_text().splitlines()[0])
    browser=pw.chromium.connect_over_cdp(f'http://127.0.0.1:{port}')
    return next(p for c in browser.contexts for p in c.pages if part in p.url)
results={}
try:
    with sync_playwright() as pw:
        dock=page_for(pw,'chat-dock','dock.html'); overlay=page_for(pw,source['id'],'featured.html')
        # The dock intentionally replaces window.eval. Use the debugger's native
        # expression evaluator to supply the incoming fixture without altering it.
        cdp=dock.context.new_cdp_session(dock)
        deadline=time.time()+30
        while time.time()<deadline:
            ready=cdp.send('Runtime.evaluate',{'expression':'typeof processInput','returnByValue':True})
            if ready.get('result',{}).get('value')=='function':break
            time.sleep(.25)
        time.sleep(8)
        event={'id':424242,'platform':'youtube','type':'youtube','chatname':'Mixer QA','chatmessage':'Private mixer integration check','textonly':True}
        cdp.send('Runtime.evaluate',{'expression':'processInput('+json.dumps({'action':'content','value':json.dumps(event)})+')','returnByValue':True})
        message=dock.locator('[data-mid="424242"]'); message.wait_for(state='visible'); message.click()
        overlay.get_by_text('Private mixer integration check').wait_for(state='visible',timeout=30000)
        # Park the dock, then take the actual featured browser source into Program.
        dock.keyboard.press('Escape')
        window.set_focus(); before=control('programCanvas').capture_as_image()
        press('takeScene'); time.sleep(2); after=control('programCanvas').capture_as_image()
        from PIL import ImageChops
        assert ImageChops.difference(before.convert('RGB'),after.convert('RGB')).getbbox(),'Overlay did not reach Program'
        window.capture_as_image().save(str(report/'social-chat-studio.png'))
        results['social_stream_dock_to_program']='passed'
        dock.locator('#clear_overlay').click()
        results['clear_featured_message']='sent'
finally:
    (report/'chat-workflow.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))
