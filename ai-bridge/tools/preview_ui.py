"""Local-only visual test fixture; not included in the APK or firmware."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,HTTPServer
import json,urllib.parse
root=Path(__file__).resolve().parents[1]
config={'wifiSSID':'Existing Network','apiBase':'https://example.com/v1','hasToken':True,'model':'fixture-model','maxTokens':1024,'tokenField':'max_tokens','customPrompt':'整理文字'}
scan_count=0
probe_count=0
probe_started=False
receive_queue=[]
def trace():
    return {'exists':True,'id':9,'boot':'fixture','kind':'connectivity_test','phase':'success','startedUnix':1793880000,'startedUptimeMs':1000,'elapsedMs':1250,'httpCode':200,'request':{'model':'fixture-model','messages':[{'role':'user','content':'请只回复 OK'}]},'result':'OK','responseRaw':'{"choices":[{"message":{"content":"OK"}}]}','error':''}
class Handler(BaseHTTPRequestHandler):
    def respond(self,data,code=200,kind='application/json'):
        if isinstance(data,dict):data=json.dumps(data,ensure_ascii=False).encode()
        self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
    def do_GET(self):
        global scan_count,probe_count
        self.path=urllib.parse.urlsplit(self.path).path
        if self.path=='/app':
            s=(root/'android/assets/index.html').read_text(encoding='utf-8')
            fixture='''<script>window.Android={load:()=>sessionStorage.getItem('state')||'{}',save:s=>sessionStorage.setItem('state',s),scan:()=>{},connect:()=>{},disconnect:()=>{},send:()=>{},copy:()=>{},openConfig:()=>{}};</script>'''
            data=s.replace('<script>',fixture+'<script>',1).encode();kind='text/html; charset=utf-8'
        elif self.path=='/config':data=(root/'firmware/web/config.html').read_bytes();kind='text/html; charset=utf-8'
        elif self.path in ['/receive','/receive-legacy']:
            data=(root/'firmware/web/receiver.html').read_bytes()
            if self.path.endswith('-legacy'):data=data.replace(b'<script>',b'<script>window.JSON=undefined;</script><script>',1)
            kind='text/html; charset=utf-8'
        elif self.path=='/api/status':self.respond({'wifiConnected':True,'wifiSSID':config['wifiSSID'],'staIP':'192.168.1.99','boot':'fixture'});return
        elif self.path.startswith('/api/'):
            if self.headers.get('X-Device-Key')!='fixture-admin':self.respond({'error':'设备管理密码不正确'},403);return
            if self.path=='/api/receive':self.respond(receive_queue[0] if receive_queue else {'type':'idle','busy':False,'error':''})
            elif self.path=='/api/config':self.respond(config)
            elif self.path=='/api/wifi/scan':
                scan_count+=1;self.respond({'scanning':False,'networks':[{'ssid':'Office 2.4G','rssi':-43,'secure':True},{'ssid':'Office 2.4G','rssi':-65,'secure':True},{'ssid':'Open Network','rssi':-70,'secure':False},{'ssid':'<Wi-Fi & "test">','rssi':-55,'secure':True}]})
            elif self.path=='/api/model-test':
                probe_count+=1;self.respond({'id':9,'phase':'success' if probe_count>1 else 'processing','reply':'OK','httpCode':200,'elapsedMs':1250,'recordSaved':True})
            elif self.path=='/api/last-call':self.respond(trace() if probe_started else {'exists':False})
            else:self.send_error(404)
            return
        else:self.send_error(404);return
        self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
    def do_POST(self):
        global probe_started,probe_count
        if self.headers.get('X-Device-Key')!='fixture-admin':self.respond({'error':'设备管理密码不正确'},403);return
        body=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))) or b'{}')
        out=root/'test-output';out.mkdir(exist_ok=True)
        with (out/'ui-fixture-requests.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps({'path':self.path,'body':body},ensure_ascii=False)+'\n')
        if self.path=='/api/config':
            config.update({k:body[k] for k in ['model','maxTokens','tokenField','customPrompt'] if k in body});config['wifiSSID']=body.get('wifiSSID',config['wifiSSID']);self.respond({'saved':True})
        elif self.path=='/api/wifi/scan':self.respond({'scanning':True})
        elif self.path=='/api/model-test':probe_started=True;probe_count=0;self.respond({'id':9},202)
        elif self.path=='/api/receive/test':receive_queue.append({'type':'text','boot':'fixture','id':17,'text':'局域网接收测试\n中文 English 😀\n<script>只作为文字显示</script>'});self.respond({'queued':True})
        elif self.path=='/api/receive/ack':
            if receive_queue and body.get('boot')==receive_queue[0]['boot'] and body.get('id')==receive_queue[0]['id']:receive_queue.pop(0)
            self.respond({'acknowledged':True})
        elif self.path=='/api/receive/release':self.respond({'released':True})
        else:self.send_error(404)
    def log_message(self,*args):pass
HTTPServer(('127.0.0.1',8768),Handler).serve_forever()
