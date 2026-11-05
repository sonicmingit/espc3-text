/* Sonic Text Bridge USB receiver. Native Win32, no C runtime required. */
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#define _CRTIMP
#include <windows.h>
#include <commdlg.h>
#include <stddef.h>
#define JSMN_STATIC
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "jsmn.h"
DWORD _tls_index;

void *memset(void *p,int value,size_t n){unsigned char *a=p;while(n--)*a++=(unsigned char)value;return p;}
void *memcpy(void *p,const void *q,size_t n){unsigned char *a=p;const unsigned char *b=q;while(n--)*a++=*b++;return p;}
void *memmove(void *p,const void *q,size_t n){unsigned char *a=p;const unsigned char *b=q;if(a<b){for(size_t i=0;i<n;i++)a[i]=b[i];}else{while(n){--n;a[n]=b[n];}}return p;}
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
int memcmp(const void*p,const void*q,size_t n){const unsigned char*a=p,*b=q;while(n--){if(*a!=*b)return(int)*a-(int)*b;a++;b++;}return 0;}
static void*alloc(SIZE_T n){return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n);}
static void dispose(void*p){if(p)HeapFree(GetProcessHeap(),0,p);}
static int wlen(const WCHAR*s){int n=0;while(s[n])n++;return n;}
static BOOL eq(const char*a,const char*b){return strlen(a)==strlen(b)&&memcmp(a,b,strlen(a))==0;}
static void utoa10(DWORD n,char*out){char tmp[16];int k=0;do{tmp[k++]=(char)('0'+n%10);n/=10;}while(n);for(int i=0;i<k;i++)out[i]=tmp[k-i-1];out[k]=0;}

#define WM_TEXTDATA (WM_APP+1)
#define WM_LINK (WM_APP+2)
#define WM_STATUS (WM_APP+3)
#define WM_FINISHED (WM_APP+4)
#define WM_READY (WM_APP+5)
#define ID_PORT 101
#define ID_REFRESH 102
#define ID_CONNECT 103
#define ID_DISCONNECT 104
#define ID_COPY 105
#define ID_SAVE 106
#define ID_CLEAR 107
#define ID_LIST 108
#define ID_TEST 109
#define TEXT_CAP 16385
#define HISTORY_CAP 200

typedef struct {DWORD id;char boot[65];WCHAR text[TEXT_CAP];} TextData;
typedef struct {char type[32];char boot[65];DWORD id;WCHAR text[TEXT_CAP];WCHAR version[32];BOOL hasText;} Frame;
static HWND window,titleLabel,helpLabel,portLabel,portCombo,refreshButton,connectButton,disconnectButton;
static HWND copyButton,saveButton,clearButton,testButton,statusLabel,deviceLabel,historyList,textEdit,footerLabel;
static HFONT font,titleFont;
static HANDLE threadHandle,stopEvent,ackEvent;
static volatile LONG testRequested;
static BOOL closing,ready;
static TextData*history[HISTORY_CAP];static int historyCount;
static WCHAR selectedPort[24];

static int hexval(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int hex4(const char*s){int n=0;for(int i=0;i<4;i++){int v=hexval(s[i]);if(v<0)return -1;n=(n<<4)|v;}return n;}
static BOOL decodeString(const char*s,int start,int end,WCHAR*out,int capacity){
 int at=0;
 for(int i=start;i<end;){unsigned int cp=(unsigned char)s[i++];
  if(cp=='\\'){
   if(i>=end)return FALSE;char e=s[i++];
   if(e=='u'){
    if(i+4>end)return FALSE;int u=hex4(s+i);if(u<0)return FALSE;i+=4;cp=(unsigned int)u;
    if(cp>=0xd800&&cp<=0xdbff){if(i+6>end||s[i]!='\\'||s[i+1]!='u')return FALSE;int low=hex4(s+i+2);if(low<0xdc00||low>0xdfff)return FALSE;cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);i+=6;}
    else if(cp>=0xdc00&&cp<=0xdfff)return FALSE;
   }else if(e=='n')cp=10;else if(e=='r')cp=13;else if(e=='t')cp=9;else if(e=='b')cp=8;else if(e=='f')cp=12;
   else if(e=='"'||e=='\\'||e=='/')cp=(unsigned char)e;else return FALSE;
  }else if(cp<32)return FALSE;
  else if(cp>=0x80){
   unsigned int min;int need;
   if(cp>=0xc2&&cp<=0xdf){need=1;min=0x80;cp&=31;}
   else if(cp>=0xe0&&cp<=0xef){need=2;min=0x800;cp&=15;}
   else if(cp>=0xf0&&cp<=0xf4){need=3;min=0x10000;cp&=7;}else return FALSE;
   if(i+need>end)return FALSE;
   for(int k=0;k<need;k++){unsigned char b=(unsigned char)s[i++];if((b&0xc0)!=0x80)return FALSE;cp=(cp<<6)|(b&63);}
   if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return FALSE;
  }
  if(cp==0)return FALSE;
  if(cp>=0x10000){if(at+2>=capacity)return FALSE;cp-=0x10000;out[at++]=(WCHAR)(0xd800+(cp>>10));out[at++]=(WCHAR)(0xdc00+(cp&1023));}
  else{if(at+1>=capacity)return FALSE;out[at++]=(WCHAR)cp;}
 }
 out[at]=0;return TRUE;
}
static int field(const char*s,jsmntok_t*t,int count,const char*name){int len=(int)strlen(name);for(int i=1;i+1<count;i++)if(t[i].parent==0&&t[i].type==JSMN_STRING&&t[i].end-t[i].start==len&&!memcmp(s+t[i].start,name,len))return i+1;return -1;}
static BOOL asciiField(const char*s,jsmntok_t*t,int i,char*out,int cap){
 if(i<0||t[i].type!=JSMN_STRING)return FALSE;WCHAR temp[128];if(cap>128||!decodeString(s,t[i].start,t[i].end,temp,cap))return FALSE;
 int n=wlen(temp);for(int k=0;k<=n;k++){if(temp[k]>127)return FALSE;out[k]=(char)temp[k];}return TRUE;
}
static BOOL uintField(const char*s,jsmntok_t*t,int i,DWORD*out){
 if(i<0||t[i].type!=JSMN_PRIMITIVE||t[i].end==t[i].start)return FALSE;DWORD n=0;
 for(int k=t[i].start;k<t[i].end;k++){if(s[k]<'0'||s[k]>'9')return FALSE;DWORD digit=(DWORD)(s[k]-'0');if(n>429496729U||(n==429496729U&&digit>5))return FALSE;n=n*10+digit;}
 *out=n;return TRUE;
}
static BOOL parseFrame(const char*s,int n,Frame*f){
 jsmn_parser p;jsmntok_t tokens[160];jsmn_init(&p);int count=jsmn_parse(&p,s,(size_t)n,tokens,160);
 if(count<1||tokens[0].type!=JSMN_OBJECT)return FALSE;
 for(int i=tokens[0].end;i<n;i++)if(s[i]!=' '&&s[i]!='\r'&&s[i]!='\t')return FALSE;
 memset(f,0,sizeof(*f));if(!asciiField(s,tokens,field(s,tokens,count,"type"),f->type,32))return FALSE;
 if(eq(f->type,"text")){
  if(!asciiField(s,tokens,field(s,tokens,count,"boot"),f->boot,65)||!uintField(s,tokens,field(s,tokens,count,"id"),&f->id))return FALSE;
  int i=field(s,tokens,count,"text");if(i<0||tokens[i].type!=JSMN_STRING||!decodeString(s,tokens[i].start,tokens[i].end,f->text,TEXT_CAP))return FALSE;f->hasText=TRUE;
 }else if(eq(f->type,"status")){
  int i=field(s,tokens,count,"version");if(i>=0&&tokens[i].type==JSMN_STRING)decodeString(s,tokens[i].start,tokens[i].end,f->version,32);
 }else if(eq(f->type,"error")||eq(f->type,"job")){
  int i=field(s,tokens,count,"error");if(i<0)i=field(s,tokens,count,"message");if(i>=0&&tokens[i].type==JSMN_STRING)decodeString(s,tokens[i].start,tokens[i].end,f->text,TEXT_CAP);
 }
 return TRUE;
}
static void postText(UINT message,const WCHAR*text){int n=wlen(text);WCHAR*copy=alloc((n+1)*sizeof(WCHAR));if(!copy)return;memcpy(copy,text,(n+1)*sizeof(WCHAR));if(!PostMessageW(window,message,0,(LPARAM)copy))dispose(copy);}
static BOOL serialWrite(HANDLE port,const char*text){DWORD sent=0,len=(DWORD)strlen(text);return WriteFile(port,text,len,&sent,0)&&sent==len;}
static DWORD WINAPI readerThread(void*unused){
 WCHAR path[48];wsprintfW(path,L"\\\\.\\%s",selectedPort);
 HANDLE port=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,0,OPEN_EXISTING,0,0);
 DWORD error=0;
 if(port==INVALID_HANDLE_VALUE){error=GetLastError();postText(WM_STATUS,error==ERROR_ACCESS_DENIED?L"串口被占用，请断开网页接收端或其他串口工具后重试。":L"无法打开串口，请检查设备连接与 USB 串口驱动。");PostMessageW(window,WM_FINISHED,error,0);return 0;}
 DCB dcb;memset(&dcb,0,sizeof(dcb));dcb.DCBlength=sizeof(dcb);
 COMMTIMEOUTS timeouts;memset(&timeouts,0,sizeof(timeouts));timeouts.ReadIntervalTimeout=MAXDWORD;timeouts.ReadTotalTimeoutConstant=100;timeouts.WriteTotalTimeoutConstant=500;
 if(!GetCommState(port,&dcb)){error=GetLastError();goto done;}
 dcb.BaudRate=115200;dcb.ByteSize=8;dcb.Parity=NOPARITY;dcb.StopBits=ONESTOPBIT;dcb.fBinary=TRUE;dcb.fParity=FALSE;
 dcb.fOutxCtsFlow=FALSE;dcb.fOutxDsrFlow=FALSE;dcb.fDtrControl=DTR_CONTROL_DISABLE;dcb.fDsrSensitivity=FALSE;dcb.fOutX=FALSE;dcb.fInX=FALSE;dcb.fRtsControl=RTS_CONTROL_DISABLE;
 if(!SetCommState(port,&dcb)||!SetCommTimeouts(port,&timeouts)){error=GetLastError();goto done;}
 PurgeComm(port,PURGE_RXCLEAR|PURGE_TXCLEAR);postText(WM_LINK,L"串口已打开，等待板子响应…");serialWrite(port,"HELLO\n");
 char*line=alloc(65536);Frame*frame=alloc(sizeof(Frame));int length=0;BOOL dropping=FALSE,responded=FALSE,warned=FALSE;
 if(!line||!frame){error=ERROR_NOT_ENOUGH_MEMORY;dispose(line);dispose(frame);goto done;}
 DWORD poll=GetTickCount()-1000,info=poll,opened=GetTickCount();
 while(WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0){
  DWORD now=GetTickCount();
  if(now-poll>=750){if(!serialWrite(port,"POLL\n")){error=GetLastError();break;}poll=now;}
  if(now-info>=3000){if(!serialWrite(port,"STATUS\n")){error=GetLastError();break;}info=now;}
  if(InterlockedExchange(&testRequested,0))serialWrite(port,"RXTEST\n");
  if(!responded&&!warned&&now-opened>6000){warned=TRUE;postText(WM_STATUS,L"尚未收到板子响应，请核对串口和固件；也可断开后重试。");}
  char bytes[512];DWORD got=0;if(!ReadFile(port,bytes,sizeof(bytes),&got,0)){error=GetLastError();break;}
  for(DWORD b=0;b<got;b++){
   if(bytes[b]!='\n'){if(!dropping){if(length<65535)line[length++]=bytes[b];else{length=0;dropping=TRUE;postText(WM_STATUS,L"设备返回的数据过长，本条未确认接收，请检查固件。");}}continue;}
   if(dropping){dropping=FALSE;length=0;continue;}
   BOOL valid=parseFrame(line,length,frame);length=0;if(!valid)continue;
   if(eq(frame->type,"status")){responded=TRUE;WCHAR text[160];wsprintfW(text,L"设备已连接 · 固件 %s · USB 串口",frame->version);postText(WM_READY,text);}
   else if(frame->hasText){
    TextData*data=alloc(sizeof(TextData));if(!data){postText(WM_STATUS,L"内存不足，本条尚未确认接收。");continue;}
    data->id=frame->id;memcpy(data->boot,frame->boot,65);memcpy(data->text,frame->text,sizeof(data->text));
    char ack[40],number[16];utoa10(frame->id,number);memcpy(ack,"ACK ",4);size_t digits=strlen(number);memcpy(ack+4,number,digits);ack[4+digits]='\n';ack[5+digits]=0;
    ResetEvent(ackEvent);
    if(!PostMessageW(window,WM_TEXTDATA,0,(LPARAM)data)){dispose(data);continue;}
    HANDLE waits[2]={stopEvent,ackEvent};DWORD result=WaitForMultipleObjects(2,waits,FALSE,6000);
    if(result==WAIT_OBJECT_0+1)serialWrite(port,ack);
   }else if(frame->text[0])postText(WM_STATUS,frame->text);
  }
 }
 serialWrite(port,"RELEASE\n");dispose(line);dispose(frame);
done:
 if(error)postText(WM_STATUS,L"串口连接已中断，请检查 USB 线或驱动后重新连接。");
 CloseHandle(port);PostMessageW(window,WM_FINISHED,error,0);return 0;
}
static void buttons(BOOL connected){EnableWindow(connectButton,!connected);EnableWindow(refreshButton,!connected);EnableWindow(portCombo,!connected);EnableWindow(disconnectButton,connected);EnableWindow(testButton,connected&&ready);}
static void refreshPorts(void){
 SendMessageW(portCombo,CB_RESETCONTENT,0,0);HKEY key;int choose=0;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DEVICEMAP\\SERIALCOMM",0,KEY_READ,&key)==ERROR_SUCCESS){
  for(DWORD i=0;;i++){WCHAR name[256],value[256];DWORD names=256,bytes=sizeof(value),type=0;LONG result=RegEnumValueW(key,i,name,&names,0,&type,(BYTE*)value,&bytes);if(result==ERROR_NO_MORE_ITEMS)break;if(result==ERROR_SUCCESS&&type==REG_SZ){value[255]=0;LRESULT pos=SendMessageW(portCombo,CB_ADDSTRING,0,(LPARAM)value);if(!lstrcmpiW(value,L"COM5"))choose=(int)pos;}}
  RegCloseKey(key);
 }
 if(SendMessageW(portCombo,CB_GETCOUNT,0,0)>0)SendMessageW(portCombo,CB_SETCURSEL,choose,0);else SetWindowTextW(portCombo,L"COM5");
}
static BOOL validPort(void){
 GetWindowTextW(portCombo,selectedPort,24);int len=wlen(selectedPort);if(len<4||len>8)return FALSE;
 for(int i=0;i<3;i++)if(selectedPort[i]>='a'&&selectedPort[i]<='z')selectedPort[i]-=32;
 if(selectedPort[0]!='C'||selectedPort[1]!='O'||selectedPort[2]!='M'||selectedPort[3]=='0')return FALSE;
 for(int i=3;i<len;i++)if(selectedPort[i]<'0'||selectedPort[i]>'9')return FALSE;return TRUE;
}
static TextData*selected(void){int i=(int)SendMessageW(historyList,LB_GETCURSEL,0,0);return i>=0&&i<historyCount?history[i]:0;}
static void normalizeLines(WCHAR*s){int len=wlen(s),extra=0;for(int i=0;i<len;i++)if(s[i]=='\n'&&(i==0||s[i-1]!='\r'))extra++;if(len+extra>=TEXT_CAP)return;int to=len+extra;s[to--]=0;for(int i=len-1;i>=0;i--){s[to--]=s[i];if(s[i]=='\n'&&(i==0||s[i-1]!='\r'))s[to--]='\r';}}
static void selectRecord(void){TextData*item=selected();SetWindowTextW(textEdit,item?item->text:L"");EnableWindow(copyButton,item!=0);EnableWindow(saveButton,item!=0);}
static void clearHistory(void){for(int i=0;i<historyCount;i++)dispose(history[i]);historyCount=0;SendMessageW(historyList,LB_RESETCONTENT,0,0);selectRecord();SetWindowTextW(footerLabel,L"记录只保留在本窗口，关闭前可保存为文本。最多保留 200 条。");}
static BOOL showMessage(TextData*data){
 for(int i=0;i<historyCount;i++)if(history[i]->id==data->id&&eq(history[i]->boot,data->boot)){dispose(data);return TRUE;}
 normalizeLines(data->text);WCHAR label[96];SYSTEMTIME now;GetLocalTime(&now);wsprintfW(label,L"第 %lu 条  %02u:%02u:%02u",data->id,now.wHour,now.wMinute,now.wSecond);
 LRESULT added=SendMessageW(historyList,LB_INSERTSTRING,0,(LPARAM)label);if(added==LB_ERR||added==LB_ERRSPACE){dispose(data);return FALSE;}
 if(historyCount==HISTORY_CAP){dispose(history[--historyCount]);SendMessageW(historyList,LB_DELETESTRING,HISTORY_CAP,0);}
 for(int i=historyCount;i>0;i--)history[i]=history[i-1];history[0]=data;historyCount++;
 SendMessageW(historyList,LB_SETCURSEL,0,0);selectRecord();WCHAR count[128];wsprintfW(count,L"本窗口保留 %u 条记录 · 关闭前可保存为 UTF-8 文本。",historyCount);SetWindowTextW(footerLabel,count);return TRUE;
}
static void copySelected(void){
 TextData*item=selected();if(!item)return;SIZE_T bytes=(wlen(item->text)+1)*sizeof(WCHAR);HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!memory)return;void*p=GlobalLock(memory);if(!p){GlobalFree(memory);return;}memcpy(p,item->text,bytes);GlobalUnlock(memory);
 if(!OpenClipboard(window)){GlobalFree(memory);SetWindowTextW(statusLabel,L"剪贴板正被使用，请稍后再复制。");return;}EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,memory)){GlobalFree(memory);SetWindowTextW(statusLabel,L"复制失败，请重试。");}else SetWindowTextW(statusLabel,L"已复制，可以粘贴到其他软件。");CloseClipboard();
}
static void saveSelected(void){
 TextData*item=selected();if(!item)return;WCHAR path[MAX_PATH]=L"文字桥接收.txt";OPENFILENAMEW dialog;memset(&dialog,0,sizeof(dialog));dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFilter=L"UTF-8 文本 (*.txt)\0*.txt\0所有文件\0*.*\0\0";dialog.lpstrFile=path;dialog.nMaxFile=MAX_PATH;dialog.lpstrDefExt=L"txt";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
 if(!GetSaveFileNameW(&dialog))return;int size=WideCharToMultiByte(CP_UTF8,0,item->text,-1,0,0,0,0);if(size<=0)return;char*utf=alloc((SIZE_T)size);if(!utf)return;WideCharToMultiByte(CP_UTF8,0,item->text,-1,utf,size,0,0);
 HANDLE file=CreateFileW(path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);BOOL ok=FALSE;if(file!=INVALID_HANDLE_VALUE){DWORD n;const BYTE bom[3]={0xef,0xbb,0xbf};ok=WriteFile(file,bom,3,&n,0)&&n==3;DWORD done=0;while(ok&&done<(DWORD)size-1){DWORD chunk=(DWORD)size-1-done;if(chunk>4096)chunk=4096;if(!WriteFile(file,utf+done,chunk,&n,0)||n!=chunk)ok=FALSE;else done+=n;}CloseHandle(file);}dispose(utf);SetWindowTextW(statusLabel,ok?L"文本已保存为 UTF-8，支持中文和换行。":L"保存失败，请检查路径与磁盘空间。");
}
static HWND control(const WCHAR*className,const WCHAR*text,DWORD style,int id){HWND h=CreateWindowExW((style&WS_BORDER)?WS_EX_CLIENTEDGE:0,className,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,(HMENU)(INT_PTR)id,GetModuleHandleW(0),0);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;}
static void layout(int w,int h){
 int m=16,gap=8,left=w>=800?215:170,content=h-238;if(content<100)content=100;
 MoveWindow(titleLabel,m,12,w-2*m,32,TRUE);MoveWindow(helpLabel,m,48,w-2*m,24,TRUE);
 MoveWindow(portLabel,m,87,44,24,TRUE);MoveWindow(portCombo,m+48,80,140,240,TRUE);MoveWindow(refreshButton,m+196,80,88,32,TRUE);MoveWindow(connectButton,m+292,80,88,32,TRUE);MoveWindow(disconnectButton,m+388,80,88,32,TRUE);
 MoveWindow(deviceLabel,m,122,w-2*m,24,TRUE);MoveWindow(statusLabel,m,150,w-2*m,24,TRUE);
 MoveWindow(copyButton,m,180,88,32,TRUE);MoveWindow(saveButton,m+96,180,100,32,TRUE);MoveWindow(clearButton,m+204,180,100,32,TRUE);MoveWindow(testButton,m+312,180,100,32,TRUE);
 MoveWindow(historyList,m,224,left,content,TRUE);MoveWindow(textEdit,m+left+gap,224,w-2*m-left-gap,content,TRUE);MoveWindow(footerLabel,m,h-30,w-2*m,24,TRUE);
}
static LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_CREATE:{window=hwnd;font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Tahoma");titleFont=CreateFontW(-24,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Tahoma");
  titleLabel=control(L"STATIC",L"文字桥 USB 接收器",0,0);SendMessageW(titleLabel,WM_SETFONT,(WPARAM)titleFont,TRUE);
  helpLabel=control(L"STATIC",L"选择板子的串口并连接。无需 Chrome、Edge、Python 或 .NET。",0,0);portLabel=control(L"STATIC",L"串口",0,0);
  portCombo=control(L"COMBOBOX",L"",CBS_DROPDOWN|WS_VSCROLL|WS_TABSTOP,ID_PORT);SendMessageW(portCombo,CB_LIMITTEXT,8,0);
  refreshButton=control(L"BUTTON",L"刷新串口",WS_TABSTOP,ID_REFRESH);connectButton=control(L"BUTTON",L"连接",WS_TABSTOP|BS_DEFPUSHBUTTON,ID_CONNECT);disconnectButton=control(L"BUTTON",L"断开",WS_TABSTOP,ID_DISCONNECT);
  deviceLabel=control(L"STATIC",L"尚未连接设备。旧 Windows 需要先能在设备管理器中看到串口。",0,0);statusLabel=control(L"STATIC",L"等待接收。USB 与局域网页面请择一连接。",0,0);
  copyButton=control(L"BUTTON",L"复制",WS_TABSTOP,ID_COPY);saveButton=control(L"BUTTON",L"保存文本",WS_TABSTOP,ID_SAVE);clearButton=control(L"BUTTON",L"清空记录",WS_TABSTOP,ID_CLEAR);testButton=control(L"BUTTON",L"测试接收",WS_TABSTOP,ID_TEST);
  historyList=control(L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_TABSTOP,ID_LIST);textEdit=control(L"EDIT",L"",WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,0);SendMessageW(textEdit,EM_SETLIMITTEXT,60000,0);
  footerLabel=control(L"STATIC",L"记录只保留在本窗口，关闭前可保存为文本。最多保留 200 条。",0,0);ackEvent=CreateEventW(0,FALSE,FALSE,0);buttons(FALSE);selectRecord();refreshPorts();return 0;}
 case WM_SIZE:layout(LOWORD(lp),HIWORD(lp));return 0;
 case WM_GETMINMAXINFO:{MINMAXINFO*i=(MINMAXINFO*)lp;i->ptMinTrackSize.x=640;i->ptMinTrackSize.y=480;return 0;}
 case WM_COMMAND:switch(LOWORD(wp)){
  case ID_REFRESH:refreshPorts();break;
  case ID_CONNECT:if(!threadHandle){if(!validPort()){SetWindowTextW(statusLabel,L"请输入有效串口，例如 COM5。");break;}ready=FALSE;InterlockedExchange(&testRequested,0);ResetEvent(ackEvent);stopEvent=CreateEventW(0,TRUE,FALSE,0);buttons(TRUE);threadHandle=CreateThread(0,0,readerThread,0,0,0);if(!threadHandle){CloseHandle(stopEvent);stopEvent=0;buttons(FALSE);SetWindowTextW(statusLabel,L"无法创建接收线程。");}}break;
  case ID_DISCONNECT:if(stopEvent){SetEvent(stopEvent);EnableWindow(disconnectButton,FALSE);SetWindowTextW(statusLabel,L"正在断开并释放串口…");}break;
  case ID_COPY:copySelected();break;case ID_SAVE:saveSelected();break;case ID_CLEAR:clearHistory();break;
  case ID_LIST:if(HIWORD(wp)==LBN_SELCHANGE)selectRecord();break;
  case ID_TEST:if(ready){InterlockedExchange(&testRequested,1);SetWindowTextW(statusLabel,L"正在运行本地接收测试，不调用真实模型接口。 ");}break;
 }return 0;
 case WM_TEXTDATA:{TextData*data=(TextData*)lp;if(closing){dispose(data);return 0;}if(showMessage(data)){SetWindowTextW(statusLabel,L"已收到文字，点击复制或保存文本。");SetEvent(ackEvent);}else SetWindowTextW(statusLabel,L"无法显示内容，本条未确认接收。");return 0;}
 case WM_LINK:case WM_STATUS:case WM_READY:{WCHAR*text=(WCHAR*)lp;if(!closing){if(message==WM_READY){ready=TRUE;SetWindowTextW(deviceLabel,text);EnableWindow(testButton,threadHandle!=0);}else SetWindowTextW(statusLabel,text);}dispose(text);return 0;}
 case WM_FINISHED:if(threadHandle){WaitForSingleObject(threadHandle,1000);CloseHandle(threadHandle);threadHandle=0;}if(stopEvent){CloseHandle(stopEvent);stopEvent=0;}ready=FALSE;buttons(FALSE);SetWindowTextW(deviceLabel,L"设备已断开，可重新选择串口连接。");if(!wp)SetWindowTextW(statusLabel,L"已断开，串口已释放。");if(closing)DestroyWindow(hwnd);return 0;
 case WM_CLOSE:if(threadHandle){closing=TRUE;SetEvent(stopEvent);EnableWindow(hwnd,FALSE);}else DestroyWindow(hwnd);return 0;
 case WM_DESTROY:clearHistory();if(ackEvent)CloseHandle(ackEvent);DeleteObject(font);DeleteObject(titleFont);PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(hwnd,message,wp,lp);
}
static int selftest(void){
 Frame*f=alloc(sizeof(Frame));if(!f)return 10;
 const char*good="{\"type\":\"text\",\"boot\":\"abc123\",\"id\":4294967295,\"text\":\"\\u4e2d\\u6587\\nEnglish \\ud83d\\ude00 \\\"\\\\\"}";
 if(!parseFrame(good,(int)strlen(good),f)||f->id!=4294967295U||f->text[0]!=0x4e2d||f->text[1]!=0x6587||f->text[2]!=10){dispose(f);return 11;}
 const char*raw="{\"type\":\"text\",\"boot\":\"b\",\"id\":7,\"text\":\"中文 😀\"}";
 if(!parseFrame(raw,(int)strlen(raw),f)||f->text[0]!=0x4e2d||f->text[3]!=0xd83d||f->text[4]!=0xde00){dispose(f);return 12;}
 const char*bad="{\"type\":\"text\",\"boot\":\"b\",\"id\":1,\"text\":\"\\ud800\"}";
 if(parseFrame(bad,(int)strlen(bad),f)){dispose(f);return 13;}
 const char*nul="{\"type\":\"text\",\"boot\":\"b\",\"id\":1,\"text\":\"\\u0000\"}";
 if(parseFrame(nul,(int)strlen(nul),f)){dispose(f);return 14;}
 WCHAR lines[16]=L"a\nb\r\nc";normalizeLines(lines);if(lstrcmpW(lines,L"a\r\nb\r\nc")){dispose(f);return 15;}
 dispose(f);DWORD n;const char*ok="PASS: JSON framing, UTF-8 Chinese, Unicode surrogate pairs, line endings, invalid input rejection\n";WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),ok,(DWORD)strlen(ok),&n,0);return 0;
}
void __cdecl mainCRTStartup(void){
 const WCHAR*command=GetCommandLineW();for(int i=0;command[i];i++)if(command[i]=='-'&&command[i+1]=='-'&&command[i+2]=='s'){ExitProcess((UINT)selftest());}
 HINSTANCE instance=GetModuleHandleW(0);WNDCLASSW cls;memset(&cls,0,sizeof(cls));cls.lpfnWndProc=windowProc;cls.hInstance=instance;cls.hCursor=LoadCursorW(0,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);cls.lpszClassName=L"SonicTextReceiverNative";cls.hIcon=LoadIconW(0,IDI_APPLICATION);if(!RegisterClassW(&cls))ExitProcess(1);
 int width=920,height=680,screenW=GetSystemMetrics(SM_CXSCREEN),screenH=GetSystemMetrics(SM_CYSCREEN);if(width>screenW-32)width=screenW-32;if(height>screenH-64)height=screenH-64;
 HWND hwnd=CreateWindowExW(0,cls.lpszClassName,L"Sonic 文字桥 · USB 接收器",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,width,height,0,0,instance,0);if(!hwnd)ExitProcess(2);ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
 MSG msg;while(GetMessageW(&msg,0,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}ExitProcess((UINT)msg.wParam);
}
