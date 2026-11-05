"""Real BLE -> firmware -> USB smoke test, using synthetic text only."""
import asyncio,json,time,sys
from pathlib import Path
import serial
from bleak import BleakScanner,BleakClient

sys.stdout.reconfigure(encoding='utf-8')
SERVICE='6e400001-b5a3-f393-e0a9-e50e24dcca9e'
RX='6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX='6e400003-b5a3-f393-e0a9-e50e24dcca9e'
records=[]

async def main():
    port=serial.Serial(port=None,baudrate=115200,timeout=.15)
    port.dtr=False;port.rts=False;port.port='COM5';port.open()
    def serial_send(c):port.write((c+'\n').encode());port.flush()
    async def usb(kind,timeout=15):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            line=port.readline()
            if line:
                try:value=json.loads(line)
                except Exception:continue
                records.append({'usb':value})
                if value.get('type')==kind:return value
            await asyncio.sleep(.02)
        raise AssertionError('USB timeout '+kind)
    try:
        serial_send('HELLO');status=await usb('status');assert status['version']=='2.0.0'
        if status['pending']:
            serial_send('POLL');old=await usb('text')
            assert old['text']=='模型处理测试通过\n中文 English 😀','Unexpected user content; stop test'
            serial_send('ACK '+str(old['id']))
        device=await BleakScanner.find_device_by_filter(lambda d,a: SERVICE in a.service_uuids,timeout=12)
        assert device and device.name=='Sonic-AI','Sonic-AI service not found'
        buffer=bytearray();notifications=asyncio.Queue()
        def notify(_,data):
            nonlocal buffer
            buffer.extend(data)
            while b'\n' in buffer:
                line,_,buffer=buffer.partition(b'\n')
                try:value=json.loads(line.decode('utf-8'))
                except Exception:continue
                records.append({'ble':value});notifications.put_nowait(value)
        cleaner=BleakClient(device)
        try:await cleaner.unpair()
        except Exception:pass
        await asyncio.sleep(1)
        async with BleakClient(device,timeout=30,winrt={'use_cached_services':False}) as client:
            await client.pair(protection_level=2)
            assert client.is_connected
            await client.start_notify(TX,notify)
            async def send(value):
                data=(json.dumps(value,ensure_ascii=False)+'\n').encode()
                # Intentionally split Chinese and emoji byte sequences across ATT writes.
                for start in range(0,len(data),7):await client.write_gatt_char(RX,data[start:start+7],response=True)
            async def ble(kind,timeout=15):
                until=asyncio.get_running_loop().time()+timeout
                while True:
                    value=await asyncio.wait_for(notifications.get(),max(.1,until-asyncio.get_running_loop().time()))
                    if value.get('type')==kind:return value
            await send({'op':'status'});assert (await ble('status'))['version']=='2.0.0'
            text='真实 BLE 分包测试\n中文 English 😀\n引号" 反斜杠\\'
            await send({'op':'send','pin':'SonicC3-2026','mode':'raw','requestId':'ble-test-1','text':text})
            accepted=await ble('accepted');serial_send('POLL');message=await usb('text');assert message['text']==text
            assert accepted['id']==message['id']
            serial_send('POLL');assert (await usb('text'))['text']==text
            serial_send('ACK '+str(message['id']+99));serial_send('STATUS');assert (await usb('status'))['pending']==1
            serial_send('ACK '+str(message['id']));serial_send('STATUS');assert (await usb('status'))['pending']==0
            await send({'op':'send','pin':'wrong','text':'not accepted','mode':'raw'})
            assert '密码' in (await ble('error'))['error']
            await send({'op':'config','pin':'SonicC3-2026','config':{'apiBase':'https://api.openai.com/v1','model':'fixture-model','apiToken':'fixture-token-not-real','maxTokens':512}})
            await ble('saved');configured=await ble('status');assert configured['hasToken'] and configured['model']=='fixture-model'
            assert 'apiToken' not in configured and 'fixture-token-not-real' not in json.dumps(configured)
            await send({'op':'config','pin':'SonicC3-2026','config':{'maxTokens':1}})
            assert '64' in (await ble('error'))['error']
            await send({'op':'config','pin':'SonicC3-2026','config':{'model':'','clearToken':True,'maxTokens':1024}})
            await ble('saved');restored=await ble('status');assert not restored['hasToken'] and restored['model']==''
            serial_send('SELFTEST')
            event=await usb('job');
            while event.get('phase') not in ['ready','error']:event=await usb('job')
            assert event['phase']=='ready',event
            serial_send('POLL');result=await usb('text');assert result['text']=='模型处理测试通过\n中文 English 😀'
            serial_send('ACK '+str(result['id']));serial_send('STATUS');assert (await usb('status'))['pending']==0
            await client.stop_notify(TX)
        print('PASS: real BLE encrypted connection, UTF-8 chunk assembly, USB transfer, ACK, config validation and API mock')
    finally:port.close()
    out=Path(__file__).resolve().parents[1]/'test-output/ble-integration.json'
    out.write_text(json.dumps({'passed':True,'checks':['BLE discovery','encrypted BLE connection','UTF-8 fragmented writes','raw BLE to USB','repeat before ACK','wrong ACK ignored','correct ACK clears queue','wrong device key rejected','BLE model config','Token not exposed','invalid token limit rejected','config restored','OpenAI-compatible HTTP request and response to USB'], 'records':records},ensure_ascii=False,indent=2),encoding='utf-8')

asyncio.run(main())
