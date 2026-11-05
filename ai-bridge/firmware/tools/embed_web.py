from pathlib import Path
root=Path(__file__).resolve().parents[1]
html=(root/'web/config.html').read_text(encoding='utf-8')
(root/'src/config_page.h').write_text('#pragma once\nstatic const char CONFIG_PAGE[] PROGMEM=R"SONICHTML('+html+')SONICHTML";\n',encoding='utf-8')
apk=(root.parent/'dist/SonicTextBridge-v2.0.0.apk').read_bytes()
lines=[','.join('0x%02x'%x for x in apk[i:i+20]) for i in range(0,len(apk),20)]
(root/'src/android_apk.h').write_text('#pragma once\nstatic const uint8_t ANDROID_APK[] PROGMEM={\n'+',\n'.join(lines)+'\n};\n',encoding='utf-8')
receiver=(root/'web/receiver.html').read_text(encoding='utf-8')
(root/'src/lan_receiver_page.h').write_text('#pragma once\nstatic const char LAN_RECEIVER_PAGE[] PROGMEM=R"SONICHTML('+receiver+')SONICHTML";\n',encoding='utf-8')
exe=(root.parent/'dist/SonicTextReceiver-x86.exe').read_bytes()
lines=[','.join('0x%02x'%x for x in exe[i:i+20]) for i in range(0,len(exe),20)]
(root/'src/receiver_exe.h').write_text('#pragma once\nstatic const uint8_t RECEIVER_EXE[] PROGMEM={\n'+',\n'.join(lines)+'\n};\n',encoding='utf-8')
