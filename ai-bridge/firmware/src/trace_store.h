#pragma once
#include <LittleFS.h>

bool storageReady=false;
SemaphoreHandle_t traceMutex=nullptr;
String historyError;

void initTraceStorage(){
 traceMutex=xSemaphoreCreateMutex();
 storageReady=LittleFS.begin(false);
 // Earlier firmware never used this partition. Format it only on first use.
 if(!storageReady&&!prefs.getBool("traceInit",false)){
  if(LittleFS.format())storageReady=LittleFS.begin(false);
 }
 if(storageReady)prefs.putBool("traceInit",true);
 else historyError="调用记录存储区无法挂载";
}
void writeJsonString(Print&out,const String&value){
 out.write('"');
 for(size_t i=0;i<value.length();++i){uint8_t c=value[i];
  if(c=='"'||c=='\\'){out.write('\\');out.write(c);}
  else if(c<32){char escaped[7];snprintf(escaped,sizeof(escaped),"\\u%04x",c);out.print(escaped);}
  else out.write(c);
 }
 out.write('"');
}
bool saveTrace(const Job*j,const String&payload,const String&raw,const String&result,
 const String&error,const char*phase,int httpCode,uint32_t elapsed,uint32_t startedUnix,uint32_t startedUptime){
 if(!storageReady||xSemaphoreTake(traceMutex,pdMS_TO_TICKS(3000))!=pdTRUE)return false;
 const char*target=j->test?"/selftest.json":"/latest.json";
 const char*temporary=j->test?"/selftest.tmp":"/latest.tmp";
 File file=LittleFS.open(temporary,FILE_WRITE);
 if(!file){xSemaphoreGive(traceMutex);return false;}
 file.print("{\"exists\":true,\"id\":");file.print(j->id);
 file.print(",\"boot\":");writeJsonString(file,bootId);
 file.print(",\"kind\":");writeJsonString(file,j->probe?"connectivity_test":"processing");
 file.print(",\"phase\":");writeJsonString(file,phase);
 file.print(",\"mode\":");writeJsonString(file,j->mode);
 file.print(",\"model\":");writeJsonString(file,j->model);
 String url=j->url;int query=url.indexOf('?');if(query>=0)url=url.substring(0,query);
 file.print(",\"url\":");writeJsonString(file,url);
 file.print(",\"startedUnix\":");file.print(startedUnix);
 file.print(",\"startedUptimeMs\":");file.print(startedUptime);
 file.print(",\"elapsedMs\":");file.print(elapsed);
 file.print(",\"httpCode\":");file.print(httpCode);
 file.print(",\"request\":");file.print(payload.length()?payload:"null");
 file.print(",\"responseRaw\":");writeJsonString(file,raw);
 file.print(",\"result\":");writeJsonString(file,result);
 file.print(",\"error\":");writeJsonString(file,error);file.print('}');
 file.flush();bool ok=file.getWriteError()==0;file.close();
 // LittleFS rename atomically replaces the prior completed record.
 if(ok)ok=LittleFS.rename(temporary,target);
 if(!ok)LittleFS.remove(temporary);
 xSemaphoreGive(traceMutex);return ok;
}
