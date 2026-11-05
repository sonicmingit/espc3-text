#pragma once

String receiverOwner;
uint32_t receiverLastSeen=0;
const uint32_t RECEIVER_LEASE_MS=8000;
bool validClient(const String&id){
 if(!id.length()||id.length()>64)return false;
 for(size_t i=0;i<id.length();i++){char c=id[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;}
 return true;
}
bool claimReceiver(const String&owner){
 if(receiverOwner.length()&&receiverOwner!=owner&&(uint32_t)(millis()-receiverLastSeen)<RECEIVER_LEASE_MS)return false;
 receiverOwner=owner;receiverLastSeen=millis();return true;
}
void releaseReceiver(const String&owner){if(receiverOwner==owner)receiverOwner="";}
String outputFrame(){
 if(!outputCount)return "{\"type\":\"idle\",\"busy\":"+String(busy?"true":"false")+",\"phase\":"+quoted(lastPhase)+",\"error\":"+quoted(lastError)+"}";
 const Output&o=output[outputHead];return "{\"type\":\"text\",\"boot\":"+quoted(bootId)+",\"id\":"+String(o.id)+",\"text\":"+quoted(o.text)+"}";
}
bool acknowledgeOutput(uint32_t id){
 if(!outputCount||id!=output[outputHead].id)return false;
 lastAck=id;output[outputHead].text="";outputHead=(outputHead+1)%4;--outputCount;phase("delivered",id);return true;
}
String receiverTest(const String&owner){
 if(!claimReceiver(owner))return "其他接收端正在使用，请先断开后等待 8 秒";
 if(busy||outputCount)return "正在处理或仍有待收内容，请先完成接收";
 Output&o=output[(outputHead+outputCount)%4];o.id=++sequence;
 o.text=owner=="usb"?"USB 接收测试\n中文 English 😀":"局域网接收测试\n中文 English 😀";
 ++outputCount;currentJobId=o.id;phase("ready",o.id);return "";
}
void registerReceiverRoutes(){
 server.on("/receive",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",LAN_RECEIVER_PAGE);});
 server.on("/receiver.exe",HTTP_GET,[]{server.sendHeader("Content-Disposition","attachment; filename=SonicTextReceiver-x86.exe");server.send_P(200,"application/octet-stream",(const char*)RECEIVER_EXE,sizeof(RECEIVER_EXE));});
 server.on("/api/receive",HTTP_GET,[]{
  if(!auth())return;String client=server.arg("client");if(!validClient(client)){reply(400,"{\"error\":\"接收端标识无效，请刷新页面\"}");return;}
  if(!claimReceiver("lan:"+client)){reply(409,"{\"error\":\"其他接收端正在接收，请先断开，并等待 8 秒后重试\"}");return;}
  reply(200,outputFrame());
 });
 server.on("/api/receive/ack",HTTP_POST,[]{
  if(!auth())return;if(server.arg("plain").length()>512){reply(413,"{}");return;}
  StaticJsonDocument<768>d;if(deserializeJson(d,server.arg("plain"))){reply(400,"{}");return;}
  String client=d["client"]|"";
  if(!validClient(client)||!claimReceiver("lan:"+client)){reply(409,"{\"error\":\"当前由其他接收端接收\"}");return;}
  if(d["boot"].as<String>()!=bootId||!d["id"].is<uint32_t>()){reply(409,"{\"error\":\"设备已重启，请重新接收\"}");return;}
  uint32_t id=d["id"].as<uint32_t>();
  if(acknowledgeOutput(id)||id==lastAck)reply(200,"{\"acknowledged\":true}");
  else reply(409,"{\"error\":\"内容已变化，请重新接收\"}");
 });
 server.on("/api/receive/release",HTTP_POST,[]{
  if(!auth())return;StaticJsonDocument<256>d;if(deserializeJson(d,server.arg("plain"))){reply(400,"{}");return;}
  String client=d["client"]|"";if(validClient(client))releaseReceiver("lan:"+client);reply(200,"{\"released\":true}");
 });
 server.on("/api/receive/test",HTTP_POST,[]{
  if(!auth())return;StaticJsonDocument<256>d;if(deserializeJson(d,server.arg("plain"))){reply(400,"{}");return;}
  String client=d["client"]|"";if(!validClient(client)){reply(400,"{}");return;}
  String error=receiverTest("lan:"+client);if(error.length())reply(409,"{\"error\":"+quoted(error)+"}");else reply(200,"{\"queued\":true}");
 });
}
