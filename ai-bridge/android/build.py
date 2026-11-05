"""Build a dependency-free native Android APK with the project-local official SDK."""
from pathlib import Path
import os,subprocess,zipfile

root=Path(__file__).resolve().parent
workspace=root.parents[1]
sdk=Path(os.environ.get('ANDROID_SDK_ROOT',str(workspace/'.tools/android-sdk')))
tools=Path(os.environ.get('ANDROID_BUILD_TOOLS',str(sdk/'build-tools/35.0.0')))
if not tools.exists():tools=sdk/'android-15'  # Official Windows archive layout.
platform=sdk/'platforms/android-35/android.jar'
if not platform.exists():platform=sdk/'android-35/android.jar'
jdk=Path(os.environ.get('JAVA_HOME',''))
if not (jdk/'bin/javac.exe').exists():
 raise SystemExit('Set JAVA_HOME to a JDK 17 installation; see docs/BUILD.md')
build=root/'build';build.mkdir(exist_ok=True)
classes=build/'classes';classes.mkdir(exist_ok=True)
dex=build/'dex';dex.mkdir(exist_ok=True)
env=dict(os.environ,JAVA_HOME=str(jdk));env['PATH']=str(jdk/'bin')+os.pathsep+env['PATH']
def run(*args):
 print('Running',Path(str(args[0])).name,flush=True)
 subprocess.run([str(a) for a in args],check=True,env=env,cwd=root)
run(tools/'aapt2.exe','compile','--dir',root/'res','-o',build/'resources.zip')
run(tools/'aapt2.exe','link','-o',build/'unsigned.apk','--manifest',root/'AndroidManifest.xml','-I',platform,'--min-sdk-version','26','--target-sdk-version','35','-A',root/'assets',build/'resources.zip')
javafiles=list((root/'src').rglob('*.java'))
run(jdk/'bin/javac.exe','-encoding','UTF-8','--release','8','-classpath',platform,'-d',classes,*javafiles)
classfiles=list(classes.rglob('*.class'))
run(jdk/'bin/java.exe','-cp',tools/'lib/d8.jar','com.android.tools.r8.D8','--release','--min-api','26','--lib',platform,'--output',dex,*classfiles)
with zipfile.ZipFile(build/'unsigned.apk','a',compression=zipfile.ZIP_DEFLATED) as z:
 for f in dex.glob('*.dex'):z.write(f,f.name)
run(tools/'zipalign.exe','-f','-p','4',build/'unsigned.apk',build/'aligned.apk')
key=Path(os.environ.get('SONIC_KEYSTORE',str(root/'signing/sonic-test.jks')));key.parent.mkdir(parents=True,exist_ok=True)
keypass=os.environ.get('SONIC_KEY_PASSWORD','android')
storepass=os.environ.get('SONIC_STORE_PASSWORD','android')
alias=os.environ.get('SONIC_KEY_ALIAS','sonic')
# Use environment password sources so secrets do not appear in command lines.
env['SONIC_BUILD_STORE_PASSWORD']=storepass;env['SONIC_BUILD_KEY_PASSWORD']=keypass
if not key.exists():run(jdk/'bin/keytool.exe','-genkeypair','-keystore',key,'-storepass:env','SONIC_BUILD_STORE_PASSWORD','-keypass:env','SONIC_BUILD_KEY_PASSWORD','-alias',alias,'-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Sonic Text Bridge Test')
out=root.parent/'dist/SonicTextBridge-v2.0.0.apk'
out.parent.mkdir(parents=True,exist_ok=True)
run(jdk/'bin/java.exe','-jar',tools/'lib/apksigner.jar','sign','--ks',key,'--ks-key-alias',alias,'--ks-pass','env:SONIC_BUILD_STORE_PASSWORD','--key-pass','env:SONIC_BUILD_KEY_PASSWORD','--out',out,build/'aligned.apk')
run(jdk/'bin/java.exe','-jar',tools/'lib/apksigner.jar','verify','--verbose',out)
print('APK:',out)
