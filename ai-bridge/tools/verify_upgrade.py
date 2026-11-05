"""Verify v2.1 on the board without replacing user settings or latest-call records."""
import json,time,subprocess,sys
from pathlib import Path
import serial

sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[1]
records=[]
def open_port():
    s=serial.Serial(port=None,baudrate=115200,timeout=.2)
    s.dtr=False;s.rts=False;s.port='COM5';s.open();return s
port=open_port()
def send(command):port.write((command+'\n').encode());port.flush()
def wait(predicate,timeout=20):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        try:value=json.loads(port.readline())
        except Exception:continue
        if predicate(value):return value
    raise AssertionError('Device response timed out')
def status():send('HELLO');return wait(lambda x:x.get('type')=='status')
def run_case(command,phase):
    send(command)
    event=wait(lambda x:x.get('type')=='job' and x.get('phase') in ['test_ok','test_error','ready','error'])
    assert event['phase']==phase,event
    send('TRACESELF');trace=wait(lambda x:x.get('exists') is True)
    assert trace['request']['messages'][1]['content']=='中文输入\nHello 😀'
    assert 'Authorization' not in json.dumps(trace) and 'selftest-token' not in json.dumps(trace)
    assert trace['kind']=='connectivity_test'
    records.append({'case':command,'phase':trace['phase'],'httpCode':trace['httpCode']})
    return trace
try:
    first=status();assert first['version']=='2.1.0' and first['historyReady']
    assert first['apOpen'] is True
    assert not first['busy'] and first['pending']==0,'User data active; run later'
    send('SCAN');scan=wait(lambda x:'scanning'in x)
    until=time.monotonic()+20
    while scan['scanning'] and time.monotonic()<until:
        time.sleep(.7);send('SCANRESULT');scan=wait(lambda x:'scanning'in x)
    assert not scan['scanning'] and not scan.get('error'),scan.get('error')
    records.append({'case':'wifi scan','networkCount':len(scan['networks'])})
    successful=run_case('SELFTEST PROBE','test_ok')
    assert successful['httpCode']==200 and successful['result']=='模型处理测试通过\n中文 English 😀'
    assert status()['pending']==0,'Connectivity test must not enqueue PC output'
    failed=run_case('SELFTEST FAIL','test_error')
    assert failed['httpCode']==401 and 'fixture auth error'in failed['responseRaw']
    assert status()['pending']==0
    send('PROBE');probe=wait(lambda x:'recordSaved'in x)
    assert probe['phase']=='error' and probe['recordSaved'] is True
    port.close()
    subprocess.run([sys.executable,'-m','esptool','--chip','esp32c3','--port','COM5','read-mac'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    port=open_port();new=status();assert new['boot']!=first['boot'] and new['historyReady']
    send('TRACESELF');persisted=wait(lambda x:x.get('exists') is True)
    assert persisted==failed,'Saved trace changed across reset'
    records.append({'case':'trace persists across reset','passed':True})
    send('CLEARSELF');assert wait(lambda x:'cleared'in x)['cleared']
    assert status()['pending']==0
finally:port.close()
(root/'test-output/upgrade-v2.1.json').write_text(json.dumps({'passed':True,'records':records,'userModelSettingsChanged':False,'userLatestCallOverwritten':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS: real Wi-Fi scan; model probe success/error; no PC output from probe; trace retained across reset; test files cleared')
