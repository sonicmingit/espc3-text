from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent
workspace=root.parents[1]
zig=Path(os.environ.get('ZIG_EXE',str(workspace/'.tools/zig-windows-x86_64-0.13.0/zig.exe')))
output=root.parent/'dist/SonicTextReceiver-x86.exe'
output.parent.mkdir(parents=True,exist_ok=True)
if not zig.exists():raise SystemExit('Set ZIG_EXE to the Zig 0.13.0 compiler; see docs/BUILD.md')
env=dict(os.environ)
env['ZIG_GLOBAL_CACHE_DIR']=str(workspace/'.tools/zig-cache')
cmd=[str(zig),'cc','-target','x86-windows-gnu','-march=i386','-mno-sse','-mno-sse2','-Os','-fno-builtin','-fno-stack-protector','-fno-stack-check','-nostdlib',str(root/'receiver.c'),'-o',str(output),'-Wl,--entry,_mainCRTStartup','-Wl,--subsystem,windows','-Wl,--major-os-version,5','-Wl,--minor-os-version,1','-Wl,--major-subsystem-version,5','-Wl,--minor-subsystem-version,1','-Wl,--stack,2097152','-lkernel32','-luser32','-lgdi32','-ladvapi32','-lcomdlg32']
cmd[2:2]=['-isystem',str(zig.parent/'lib/libc/include/any-windows-any')]
cmd=[x.replace('--entry,_mainCRTStartup','--entry,mainCRTStartup') for x in cmd]
subprocess.run(cmd,env=env,check=True,cwd=root)
print(output)
subprocess.run([str(output),'--selftest'],check=True,cwd=root)
