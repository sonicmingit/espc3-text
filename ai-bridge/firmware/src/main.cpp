#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <esp_http_client.h>
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);
#include <time.h>
#include "config_page.h"
#include "android_apk.h"
#include "lan_receiver_page.h"
#include "receiver_exe.h"

const char *SERVICE_UUID="6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const char *RX_UUID="6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const char *TX_UUID="6e400003-b5a3-f393-e0a9-e50e24dcca9e";
const size_t INPUT_LIMIT=4096, OUTPUT_LIMIT=8192, REPLY_LIMIT=32768;
struct Config {
  String ssid, password, base="https://api.openai.com/v1", token, model;
  String deviceKey="SonicC3-2026", custom="请整理以下文字，保留原意，只返回整理后的内容。";
  String tokenField="max_tokens";
  uint16_t maxTokens=1024;
} cfg;
struct Job {uint32_t id; String text,mode,prompt,url,token,model,field;uint16_t maxTokens;bool test=false,probe=false;};
struct Result {uint32_t id;String text,error;bool probe=false,test=false,recordSaved=false;int httpCode=0;uint32_t elapsedMs=0;};
struct Output {uint32_t id;String text;};
Output output[4];uint8_t outputHead=0,outputCount=0;
uint32_t sequence=0,lastAck=0;
String bootId,serialInput,lastRequest,lastPhase="idle",lastError;
uint32_t lastRequestId=0,currentJobId=0;
bool busy=false, mockEnabled=false, mirrorEvents=false;
bool mockFailure=false;
String probeReply,probeError,probePhase="idle";
uint32_t probeId=0,probeElapsed=0;
int probeHttpCode=0;
bool probeRecordSaved=false;
volatile bool bleConnected=false, restartAdvertising=false;
String bleInput,bleOutgoing;size_t bleOffset=0;uint32_t lastBleTick=0;
NimBLECharacteristic *txChar=nullptr;
QueueHandle_t incoming, jobs, results, events;
WebServer server(80);Preferences prefs;
uint32_t reconnectAt=0;
#include "trace_store.h"

String encode(JsonDocument &doc){String s;serializeJson(doc,s);return s;}
String quoted(const String&s){DynamicJsonDocument d(s.length()+64);d.set(s);return encode(d);}
String promptFor(const String&m){
 if(m=="polish")return "润色以下文字，改善表达和标点，保留事实与原意，不添加未提供的信息。只返回润色后的文字。";
 if(m=="summary")return "将以下文字整理成简明摘要，突出重点，保留关键事实与数字。只返回摘要。";
 if(m=="todo")return "从以下文字中提取待办事项，每项单独一行。只使用原文提供的负责人和时间，缺失时不要编造。";
 if(m=="translate")return "将以下文字翻译为自然准确的英文，只返回译文。";
 if(m=="custom")return cfg.custom;
 return "";
}
void queueEvent(const String&value){
 if(mirrorEvents)Serial.println(value);
 if(!bleConnected)return;
 String *s=new String(value);
 if(xQueueSend(events,&s,0)!=pdTRUE)delete s;
}
void phase(const String&p,uint32_t id,const String&error=""){
 lastPhase=p;lastError=error;
 String s="{\"type\":\"job\",\"phase\":"+quoted(p)+",\"id\":"+String(id)+",\"error\":"+quoted(error)+"}";
 queueEvent(s);Serial.println(s);
}
String statusJson(){
 DynamicJsonDocument d(1800);d["type"]="status";d["version"]="2.2.0";d["boot"]=bootId;
 wifi_config_t apConfig={};if(esp_wifi_get_config(WIFI_IF_AP,&apConfig)==ESP_OK)d["apOpen"]=apConfig.ap.authmode==WIFI_AUTH_OPEN;
 d["ssid"]="Sonic-AI";d["ip"]="192.168.4.1";d["wifiConnected"]=WiFi.status()==WL_CONNECTED;
 d["wifiSSID"]=cfg.ssid;d["staIP"]=WiFi.localIP().toString();d["pending"]=outputCount;
 d["busy"]=busy;d["phase"]=lastPhase;d["jobId"]=currentJobId;d["error"]=lastError;
 d["maxBytes"]=INPUT_LIMIT;d["hasToken"]=cfg.token.length()>0;d["model"]=cfg.model;
 d["heap"]=ESP.getFreeHeap();d["historyReady"]=storageReady;d["historyError"]=historyError;return encode(d);
}
void reply(int code,const String&s){server.sendHeader("Cache-Control","no-store");server.send(code,"application/json; charset=utf-8",s);}
bool auth(){if(server.header("X-Device-Key")==cfg.deviceKey)return true;reply(403,"{\"error\":\"设备管理密码不正确\"}");return false;}
void loadConfig(){
 prefs.begin("sonic-ai",false);cfg.ssid=prefs.getString("ssid","");cfg.password=prefs.getString("wifiPass","");
 cfg.base=prefs.getString("base",cfg.base);cfg.token=prefs.getString("token","");cfg.model=prefs.getString("model","");
 cfg.deviceKey=prefs.getString("deviceKey",cfg.deviceKey);cfg.custom=prefs.getString("custom",cfg.custom);
 cfg.tokenField=prefs.getString("field",cfg.tokenField);cfg.maxTokens=prefs.getUShort("maxTokens",1024);
}
void persistConfig(){
 prefs.putString("ssid",cfg.ssid);prefs.putString("wifiPass",cfg.password);prefs.putString("base",cfg.base);
 prefs.putString("token",cfg.token);prefs.putString("model",cfg.model);prefs.putString("deviceKey",cfg.deviceKey);
 prefs.putString("custom",cfg.custom);prefs.putString("field",cfg.tokenField);prefs.putUShort("maxTokens",cfg.maxTokens);
}
String applyConfig(JsonVariantConst v){
 if(busy)return "模型处理中，请完成后再修改配置";
 Config next=cfg;
 if(v.containsKey("wifiSSID"))next.ssid=v["wifiSSID"].as<String>();
 if(v.containsKey("wifiPassword")){
   String password=v["wifiPassword"].as<String>();
   if(password.length()||next.ssid!=cfg.ssid||v["clearWifiPassword"]==true)next.password=password;
 }
 if(v.containsKey("apiBase")){next.base=v["apiBase"].as<String>();next.base.trim();}
 if(v.containsKey("apiToken")){String token=v["apiToken"].as<String>();token.trim();if(token.length())next.token=token;}
 if(v["clearToken"]==true)next.token="";
 if(v.containsKey("model")){next.model=v["model"].as<String>();next.model.trim();}
 if(v.containsKey("customPrompt"))next.custom=v["customPrompt"].as<String>();
 if(v.containsKey("tokenField"))next.tokenField=v["tokenField"].as<String>();
 if(v.containsKey("maxTokens"))next.maxTokens=v["maxTokens"].as<uint16_t>();
 if(v.containsKey("newDeviceKey")){String key=v["newDeviceKey"].as<String>();if(key.length()){if(key.length()<8||key.length()>64)return "管理密码需为 8 至 64 字节";next.deviceKey=key;}}
 if(next.ssid.length()>32||next.password.length()>63||next.token.length()>2048||next.model.length()>128||next.custom.length()>2048||next.base.length()>512)return "配置内容过长";
 if(!(next.base.startsWith("https://")||next.base.startsWith("http://"))||next.base.indexOf('@')>=0||next.base.indexOf('\n')>=0)return "接口地址应以 https:// 或 http:// 开头，且不包含账号信息";
 if(next.token.indexOf('\n')>=0||next.token.indexOf('\r')>=0)return "Token 不应包含换行";
 if(next.maxTokens<64||next.maxTokens>2048)return "输出 Token 上限应在 64 至 2048 之间";
 if(next.tokenField!="max_tokens"&&next.tokenField!="max_completion_tokens")return "输出参数名称不支持";
 bool wifiChanged=next.ssid!=cfg.ssid||next.password!=cfg.password;
 cfg=next;persistConfig();
 if(wifiChanged){WiFi.disconnect();if(cfg.ssid.length())WiFi.begin(cfg.ssid.c_str(),cfg.password.c_str());}
 return "";
}
String configJson(){DynamicJsonDocument d(5000);d["wifiSSID"]=cfg.ssid;d["apiBase"]=cfg.base;d["hasToken"]=cfg.token.length()>0;
 d["model"]=cfg.model;d["customPrompt"]=cfg.custom;d["tokenField"]=cfg.tokenField;d["maxTokens"]=cfg.maxTokens;return encode(d);}
String endpoint(String base){while(base.endsWith("/"))base.remove(base.length()-1);if(base.endsWith("/chat/completions"))return base;if(base.endsWith("/v1"))return base+"/chat/completions";return base+"/v1/chat/completions";}
String startModelProbe(uint32_t&id){
 if(busy)return "上一条请求正在处理，请稍后再测试";
 if(WiFi.scanComplete()==WIFI_SCAN_RUNNING)return "请等待 Wi-Fi 搜索完成后再测试";
 if(WiFi.status()!=WL_CONNECTED)return "请先连接可上网的 Wi-Fi";
 if(!cfg.model.length())return "请先配置模型名称";
 Job*j=new Job();j->id=++sequence;j->text="这是模型连通性测试，请只回复 OK。";j->mode="test";
 j->prompt="请遵照用户要求简短回复。";j->url=endpoint(cfg.base);j->token=cfg.token;
 j->model=cfg.model;j->field=cfg.tokenField;j->maxTokens=cfg.maxTokens;j->probe=true;
 id=j->id;
 if(xQueueSend(jobs,&j,0)!=pdTRUE){delete j;return "处理队列繁忙，请重试";}
 probeId=id;probePhase="processing";probeReply="";probeError="";probeElapsed=0;probeHttpCode=0;probeRecordSaved=false;
 currentJobId=id;busy=true;phase("processing",id);return "";
}
String probeJson(){
 DynamicJsonDocument d(12000);d["id"]=probeId;d["phase"]=probePhase;d["reply"]=probeReply;
 d["error"]=probeError;d["httpCode"]=probeHttpCode;d["elapsedMs"]=probeElapsed;d["recordSaved"]=probeRecordSaved;return encode(d);
}
String scanJson(bool start){
 int16_t count=WiFi.scanComplete();
 if(start&&count!=WIFI_SCAN_RUNNING){WiFi.scanDelete();count=WiFi.scanNetworks(true,true,false,180);}
 DynamicJsonDocument d(6500);d["scanning"]=count==WIFI_SCAN_RUNNING;
 if(count==WIFI_SCAN_FAILED)d["error"]="Wi-Fi 搜索失败，请稍后重试";
 JsonArray list=d.createNestedArray("networks");
 for(int i=0;count>=0&&i<count&&i<30;i++){
  String ssid=WiFi.SSID(i);if(!ssid.length())continue;
  JsonObject row=list.createNestedObject();row["ssid"]=ssid;row["rssi"]=WiFi.RSSI(i);
  row["channel"]=WiFi.channel(i);row["secure"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
 }
 return encode(d);
}
String submit(JsonVariantConst v,uint32_t&id){
 String text=v["text"].as<String>(),mode=v["mode"]|"raw",rid=v["requestId"]|"";
 String trimmed=text;trimmed.trim();
 if(!trimmed.length()||text.length()>INPUT_LIMIT)return "请输入文字，且不超过 4096 字节";
 if(rid.length()>64)return "请求编号过长";
 if(rid.length()&&rid==lastRequest){id=lastRequestId;return "";}
 if(busy)return "上一条文字正在处理，请稍后再发送";
 if(outputCount>=4)return "电脑待收队列已满，请先打开电脑接收页";
 if(mode!="raw"&&mode!="polish"&&mode!="summary"&&mode!="todo"&&mode!="translate"&&mode!="custom")return "处理方式不支持";
 if(mode!="raw"&&(WiFi.status()!=WL_CONNECTED||!cfg.model.length()))return "请先连接 Wi-Fi 并配置模型名称";
 id=++sequence;currentJobId=id;lastRequest=rid;lastRequestId=id;
 if(mode=="raw"){
  Output&o=output[(outputHead+outputCount)%4];o.id=id;o.text=text;++outputCount;
  phase("ready",id);return "";
 }
 Job*j=new Job();j->id=id;j->text=text;j->mode=mode;j->prompt=promptFor(mode);j->url=endpoint(cfg.base);
 j->token=cfg.token;j->model=cfg.model;j->field=cfg.tokenField;j->maxTokens=cfg.maxTokens;
 if(xQueueSend(jobs,&j,0)!=pdTRUE){delete j;lastRequest="";return "处理队列繁忙，请重试";}
 busy=true;phase("processing",id);return "";
}
struct HttpBody{String body;bool overflow=false;};
esp_err_t httpEvent(esp_http_client_event_t*e){
 if(e->event_id==HTTP_EVENT_ON_DATA){HttpBody*b=(HttpBody*)e->user_data;
  if(b->body.length()+e->data_len>REPLY_LIMIT){b->overflow=true;return ESP_FAIL;}
  b->body.concat((const char*)e->data,e->data_len);
 }return ESP_OK;
}
void aiWorker(void*){
 for(;;){Job*j=nullptr;xQueueReceive(jobs,&j,portMAX_DELAY);Result*r=new Result();r->id=j->id;r->probe=j->probe;r->test=j->test;
  uint32_t started=millis();time_t clock=time(nullptr);uint32_t startedUnix=clock>1700000000?(uint32_t)clock:0;
  String payload;
  {DynamicJsonDocument request(10000);request["model"]=j->model;request["stream"]=false;request[j->field]=j->maxTokens;
   JsonArray messages=request.createNestedArray("messages");JsonObject system=messages.createNestedObject();system["role"]="system";system["content"]=j->prompt;
   JsonObject user=messages.createNestedObject();user["role"]="user";user["content"]=j->text;payload=encode(request);}
  saveTrace(j,payload,"","","","processing",0,0,startedUnix,started);
  HttpBody body;
  if(j->url.startsWith("https://")&&time(nullptr)<1700000000){
   uint32_t start=millis();while(time(nullptr)<1700000000&&millis()-start<12000)delay(200);
   if(time(nullptr)<1700000000)r->error="网络时间同步失败，请检查 Wi-Fi 后重试";
  }
  if(!r->error.length()){
   esp_http_client_config_t config={};config.url=j->url.c_str();config.event_handler=httpEvent;config.user_data=&body;
   config.timeout_ms=60000;config.buffer_size=1024;config.buffer_size_tx=2048;config.crt_bundle_attach=esp_crt_bundle_attach;config.disable_auto_redirect=true;
   esp_http_client_handle_t client=esp_http_client_init(&config);
   if(!client)r->error="无法创建网络连接，请稍后重试";
   else{
    esp_http_client_set_method(client,HTTP_METHOD_POST);esp_http_client_set_header(client,"Content-Type","application/json");
    String bearer="Bearer "+j->token;if(j->token.length())esp_http_client_set_header(client,"Authorization",bearer.c_str());
    esp_http_client_set_post_field(client,payload.c_str(),payload.length());
    esp_err_t err=esp_http_client_perform(client);int code=esp_http_client_get_status_code(client);r->httpCode=code;
    esp_http_client_cleanup(client);
    if(j->token.length())body.body.replace(j->token,"[已隐藏Token]");
    if(body.overflow)r->error="接口返回超过 32 KB，请降低输出 Token 上限";
    else if(err!=ESP_OK)r->error="网络请求失败，请检查地址、网络和 HTTPS 证书";
    else if(code<200||code>=300)r->error="接口返回 HTTP "+String(code)+"，请检查地址、Token 和模型名称";
    else{
     StaticJsonDocument<256> filter;filter["choices"][0]["message"]["content"]=true;filter["choices"][0]["finish_reason"]=true;filter["error"]["message"]=true;
     DynamicJsonDocument response(16384);auto parse=deserializeJson(response,body.body,DeserializationOption::Filter(filter));
     if(parse)r->error="接口返回不是支持的 JSON 格式";
     else if(response["choices"][0]["finish_reason"]=="length")r->error="模型输出达到 Token 上限，请缩短输入或调整输出上限后重试";
     else if(!response["choices"][0]["message"]["content"].is<const char*>())r->error="接口未返回 choices[0].message.content 文本";
     else{r->text=response["choices"][0]["message"]["content"].as<String>();if(!r->text.length()||r->text.length()>OUTPUT_LIMIT){r->text="";r->error="输出为空或超过 8192 字节，请调整处理方式";}}
    }
   }
  }
  r->elapsedMs=millis()-started;
  r->recordSaved=saveTrace(j,payload,body.body,r->text,r->error,r->error.length()?"error":"success",r->httpCode,r->elapsedMs,startedUnix,started);
  delete j;xQueueSend(results,&r,portMAX_DELAY);
 }
}
class ServerCallbacks:public NimBLEServerCallbacks{
 void onConnect(NimBLEServer*)override{bleConnected=true;}
 void onDisconnect(NimBLEServer*)override{bleConnected=false;restartAdvertising=true;bleInput="";}
};
class RxCallbacks:public NimBLECharacteristicCallbacks{
 void onWrite(NimBLECharacteristic*c)override{
  std::string data=c->getValue();
  for(char ch:data){
   if(ch=='\n'){
    String*s=new String(bleInput);bleInput="";
    if(xQueueSend(incoming,&s,0)!=pdTRUE)delete s;
   }else if(bleInput.length()<12288)bleInput+=ch;
   else bleInput="";
  }
 }
};
void beginBle(){
 NimBLEDevice::init("Sonic-AI");NimBLEDevice::setMTU(247);
 NimBLEDevice::setSecurityAuth(true,false,true);
 NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
 NimBLEServer*s=NimBLEDevice::createServer();s->setCallbacks(new ServerCallbacks());
 NimBLEService*service=s->createService(SERVICE_UUID);
 NimBLECharacteristic*rx=service->createCharacteristic(RX_UUID,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_ENC,512);
 rx->setCallbacks(new RxCallbacks());txChar=service->createCharacteristic(TX_UUID,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::NOTIFY,512);
 service->start();auto adv=NimBLEDevice::getAdvertising();adv->addServiceUUID(SERVICE_UUID);adv->setScanResponse(true);adv->start();
}
void commandJson(const String&line){
 DynamicJsonDocument d(15000);if(deserializeJson(d,line)){queueEvent("{\"type\":\"error\",\"error\":\"数据格式错误\"}");return;}
 String op=d["op"]|"status";
 if(op=="status"){queueEvent(statusJson());return;}
 if(d["pin"].as<String>()!=cfg.deviceKey){queueEvent("{\"type\":\"error\",\"error\":\"设备管理密码不正确\"}");return;}
 if(op=="send"){
  uint32_t id=0;String error=submit(d.as<JsonVariantConst>(),id);
  if(error.length())queueEvent("{\"type\":\"error\",\"error\":"+quoted(error)+"}");
  else queueEvent("{\"type\":\"accepted\",\"id\":"+String(id)+",\"requestId\":"+quoted(d["requestId"]|"")+"}");
 }else if(op=="config"){
  String error=applyConfig(d["config"].as<JsonVariantConst>());
  if(error.length())queueEvent("{\"type\":\"error\",\"error\":"+quoted(error)+"}");
  else{queueEvent("{\"type\":\"saved\"}");queueEvent(statusJson());}
 }
}
#include "receiver_transport.h"
void startSelfTest(bool probe=false,bool failure=false){
 if(busy||outputCount){Serial.println("{\"type\":\"selftest\",\"error\":\"busy\"}");return;}
 mockEnabled=true;mockFailure=failure;Job*j=new Job();j->id=++sequence;j->text="中文输入\nHello 😀";j->mode="selftest";j->prompt="selftest";j->url="http://192.168.4.1/test/chat/completions";
 j->token="selftest-token";j->model="selftest-model";j->field="max_tokens";j->maxTokens=128;j->test=true;
 j->probe=probe;
 if(probe){probeId=j->id;probePhase="processing";probeReply="";probeError="";}
 currentJobId=j->id;busy=true;xQueueSend(jobs,&j,0);phase("processing",j->id);
}
void serialCommand(const String&c){
 if(c=="HELLO"||c=="STATUS")Serial.println(statusJson());
 else if(c=="POLL"){
  if(!claimReceiver("usb"))Serial.println("{\"type\":\"error\",\"message\":\"局域网接收端正在使用，请先断开\"}");
  else if(outputCount)Serial.println(outputFrame());
 }
 else if(c.startsWith("ACK ")&&outputCount){if(claimReceiver("usb"))acknowledgeOutput(strtoul(c.c_str()+4,nullptr,10));}
 else if(c=="RELEASE")releaseReceiver("usb");
 else if(c=="RXTEST"){String error=receiverTest("usb");if(error.length())Serial.println("{\"type\":\"error\",\"message\":"+quoted(error)+"}");}
 else if(c=="SELFTEST")startSelfTest();
 else if(c=="SELFTEST PROBE")startSelfTest(true);
 else if(c=="SELFTEST FAIL")startSelfTest(true,true);
 else if(c=="PROBE")Serial.println(probeJson());
 else if(c=="SCAN")Serial.println(scanJson(true));
 else if(c=="SCANRESULT")Serial.println(scanJson(false));
 else if(c=="TRACESELF"&&storageReady){xSemaphoreTake(traceMutex,portMAX_DELAY);File f=LittleFS.open("/selftest.json",FILE_READ);if(f){while(f.available())Serial.write(f.read());f.close();Serial.println();}else Serial.println("{\"exists\":false}");xSemaphoreGive(traceMutex);}
 else if(c=="CLEARSELF"&&storageReady){xSemaphoreTake(traceMutex,portMAX_DELAY);LittleFS.remove("/selftest.json");xSemaphoreGive(traceMutex);Serial.println("{\"cleared\":true}");}
 else if(c.startsWith("CMD ")){mirrorEvents=true;commandJson(c.substring(4));mirrorEvents=false;}
}
void setup(){
 Serial.begin(115200);Serial.setTxTimeoutMs(150);bootId=String((uint32_t)esp_random(),HEX);loadConfig();initTraceStorage();
 incoming=xQueueCreate(3,sizeof(String*));jobs=xQueueCreate(1,sizeof(Job*));results=xQueueCreate(1,sizeof(Result*));events=xQueueCreate(12,sizeof(String*));
 WiFi.mode(WIFI_AP_STA);WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
 WiFi.softAP("Sonic-AI",nullptr,6,false,2);if(cfg.ssid.length())WiFi.begin(cfg.ssid.c_str(),cfg.password.c_str());
 configTime(0,0,"pool.ntp.org","time.cloudflare.com","ntp.aliyun.com");
 beginBle();const char*headers[]={"X-Device-Key","Authorization"};server.collectHeaders(headers,2);
 registerReceiverRoutes();
 server.on("/",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",CONFIG_PAGE);});
 server.on("/app.apk",HTTP_GET,[]{server.sendHeader("Content-Disposition","attachment; filename=SonicTextBridge-v2.0.0.apk");server.send_P(200,"application/vnd.android.package-archive",(const char*)ANDROID_APK,sizeof(ANDROID_APK));});
 server.on("/api/status",HTTP_GET,[]{reply(200,statusJson());});
 server.on("/api/config",HTTP_GET,[]{if(auth())reply(200,configJson());});
 server.on("/api/wifi/scan",HTTP_POST,[]{if(!auth())return;if(busy){reply(409,"{\"error\":\"模型处理中，请完成后再搜索 Wi-Fi\"}");return;}reply(200,scanJson(true));});
 server.on("/api/wifi/scan",HTTP_GET,[]{if(auth())reply(200,scanJson(false));});
 server.on("/api/model-test",HTTP_POST,[]{if(!auth())return;uint32_t id=0;String error=startModelProbe(id);if(error.length())reply(409,"{\"error\":"+quoted(error)+"}");else reply(202,"{\"id\":"+String(id)+"}");});
 server.on("/api/model-test",HTTP_GET,[]{if(auth())reply(200,probeJson());});
 server.on("/api/last-call",HTTP_GET,[]{
  if(!auth())return;if(!storageReady){reply(503,"{\"error\":\"调用记录存储区不可用\"}");return;}
  if(xSemaphoreTake(traceMutex,pdMS_TO_TICKS(50))!=pdTRUE){reply(503,"{\"error\":\"调用记录正在更新，请稍后刷新\"}");return;}
  File f=LittleFS.open("/latest.json",FILE_READ);
  if(!f)reply(200,"{\"exists\":false}");else{server.sendHeader("Cache-Control","no-store");server.streamFile(f,"application/json; charset=utf-8");f.close();}
  xSemaphoreGive(traceMutex);
 });
 server.on("/api/config",HTTP_POST,[]{
  if(!auth())return;String body=server.arg("plain");if(body.length()>8192){reply(413,"{\"error\":\"配置过长\"}");return;}
  DynamicJsonDocument d(12000);if(deserializeJson(d,body)){reply(400,"{\"error\":\"JSON 格式错误\"}");return;}
  String error=applyConfig(d.as<JsonVariantConst>());if(error.length())reply(400,"{\"error\":"+quoted(error)+"}");else reply(200,"{\"saved\":true}");
 });
 server.on("/api/send",HTTP_POST,[]{
  if(!auth())return;String body=server.arg("plain");if(body.length()>8192){reply(413,"{\"error\":\"输入过长\"}");return;}
  DynamicJsonDocument d(12000);if(deserializeJson(d,body)){reply(400,"{\"error\":\"JSON 格式错误\"}");return;}
  uint32_t id=0;String error=submit(d.as<JsonVariantConst>(),id);if(error.length())reply(400,"{\"error\":"+quoted(error)+"}");else reply(202,"{\"id\":"+String(id)+"}");
 });
 server.on("/test/chat/completions",HTTP_POST,[]{
  if(!mockEnabled){reply(404,"{}");return;}DynamicJsonDocument d(4096);auto err=deserializeJson(d,server.arg("plain"));
  if(err||d["model"]!="selftest-model"||d["messages"][1]["content"]!="中文输入\nHello 😀"||server.header("Authorization")!="Bearer selftest-token"){reply(400,"{}");return;}
  if(mockFailure){reply(401,"{\"error\":{\"message\":\"fixture auth error\"}}");return;}
  reply(200,"{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":\"模型处理测试通过\\n中文 English 😀\"}}]}");
 });
 server.onNotFound([]{server.send(404,"text/plain; charset=utf-8","页面不存在");});server.begin();
 xTaskCreate(aiWorker,"ai-worker",14336,nullptr,1,nullptr);Serial.println(statusJson());
}
void loop(){
 server.handleClient();
 while(Serial.available()){char c=Serial.read();if(c=='\n'){serialInput.trim();serialCommand(serialInput);serialInput="";}else if(serialInput.length()<12288)serialInput+=c;else serialInput="";}
 String*input=nullptr;if(xQueueReceive(incoming,&input,0)==pdTRUE){commandJson(*input);delete input;}
 Result*r=nullptr;if(xQueueReceive(results,&r,0)==pdTRUE){
  busy=false;mockEnabled=false;
  if(!r->test)historyError=r->recordSaved?"":"最新调用记录保存失败，请检查存储区";
  if(r->probe){
   probeId=r->id;probePhase=r->error.length()?"error":"success";probeReply=r->text;probeError=r->error;
   probeHttpCode=r->httpCode;probeElapsed=r->elapsedMs;probeRecordSaved=r->recordSaved;
   phase(r->error.length()?"test_error":"test_ok",r->id,r->error);
  }
  else if(r->error.length())phase("error",r->id,r->error);
  else{Output&o=output[(outputHead+outputCount)%4];o.id=r->id;o.text=r->text;++outputCount;phase("ready",r->id);
   queueEvent("{\"type\":\"result\",\"id\":"+String(r->id)+",\"text\":"+quoted(r->text)+"}");}
  delete r;
 }
 if(restartAdvertising){restartAdvertising=false;NimBLEDevice::startAdvertising();}
 if(!bleConnected){bleOutgoing="";bleOffset=0;String*s=nullptr;while(xQueueReceive(events,&s,0)==pdTRUE)delete s;}
 else if(millis()-lastBleTick>=12){
  lastBleTick=millis();if(!bleOutgoing.length()){String*s=nullptr;if(xQueueReceive(events,&s,0)==pdTRUE){bleOutgoing=*s+"\n";delete s;bleOffset=0;}}
  if(bleOutgoing.length()){size_t n=min((size_t)20,bleOutgoing.length()-bleOffset);txChar->setValue((uint8_t*)bleOutgoing.c_str()+bleOffset,n);txChar->notify();bleOffset+=n;if(bleOffset==bleOutgoing.length())bleOutgoing="";}
 }
 if(cfg.ssid.length()&&WiFi.status()!=WL_CONNECTED&&WiFi.scanComplete()!=WIFI_SCAN_RUNNING&&millis()-reconnectAt>30000){reconnectAt=millis();WiFi.reconnect();}
 delay(2);
}
