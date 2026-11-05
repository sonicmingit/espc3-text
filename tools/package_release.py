"""Package already built artifacts; no signing keys or device data are read."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
project = root / 'ai-bridge'
dist = project / 'dist'
build = project / 'firmware/.pio/build/esp32c3'
out = root / 'release-assets'
out.mkdir(exist_ok=True)
version = '2.2.0'
core = Path(os.environ.get('PLATFORMIO_CORE_DIR', str(root / '.tools/pio-core')))
boot_app = core / 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
if not boot_app.exists():
    raise SystemExit('Set PLATFORMIO_CORE_DIR to the PlatformIO cache used for the firmware build.')

def digest(data):
    return hashlib.sha256(data).hexdigest()

apk = dist / 'SonicTextBridge-v2.0.0.apk'
exe = dist / 'SonicTextReceiver-x86.exe'
firmware = build / 'firmware.bin'
firmware_bytes = firmware.read_bytes()
for header, artifact in [('android_apk.h', apk), ('receiver_exe.h', exe)]:
    text = (project / 'firmware/src' / header).read_text(encoding='utf-8')
    embedded = bytes(int(h, 16) for h in re.findall(r'0x([0-9a-fA-F]{2})', text))
    assert embedded == artifact.read_bytes(), 'Stale generated header: ' + header
    assert embedded in firmware_bytes, 'Stale firmware: rebuild after embedding ' + header

assets = []
def copy(source, name):
    dest = out / name
    shutil.copyfile(source, dest)
    assets.append(dest)
    return dest

def archive(name, members):
    dest = out / name
    with zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED) as z:
        for source, path in members:
            z.write(source, path)
    assets.append(dest)

license_files = [(p, 'licenses/' + p.name) for p in sorted((root / 'licenses').iterdir()) if p.is_file()]
notices = [(root / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')] + license_files
copy(apk, apk.name)
copy(exe, 'SonicTextReceiver-v1.0.0-x86.exe')
copy(firmware, f'sonic-ai-bridge-v{version}-app.bin')
copy(dist / '电脑接收.html', 'chrome-usb-receiver.html')
copy(dist / '使用说明.md', 'USER-GUIDE-zh-CN.md')
copy(root / 'licenses/jsmn-MIT.txt', 'jsmn-MIT.txt')
archive(f'sonic-ai-bridge-firmware-v{version}.zip', [
    (build / 'bootloader.bin', 'bootloader.bin'),
    (build / 'partitions.bin', 'partitions.bin'),
    (boot_app, 'boot_app0.bin'), (firmware, 'firmware.bin'),
    (root / 'tools/flash.py', 'flash.py'),
    (root / 'docs/BUILD.md', 'FLASHING.md'),
    (root / 'docs/VALIDATION.md', 'VALIDATION.md'),
] + notices)
archive('sonic-text-receiver-v1.0.0-windows-x86.zip', [
    (exe, 'SonicTextReceiver-v1.0.0-x86.exe'),
    (dist / '电脑接收.html', 'chrome-usb-receiver.html'),
    (dist / '使用说明.md', 'USER-GUIDE-zh-CN.md'),
    (root / 'docs/VALIDATION.md', 'VALIDATION.md'),
] + notices)

manifest = {
    'release': 'v' + version,
    'builtAtUTC': datetime.now(timezone.utc).isoformat(),
    'components': {'firmware': version, 'android': '2.0.0', 'windowsReceiver': '1.0.0'},
    'appBinFlashOffset': '0x10000',
    'firmwareEmbeddedArtifactsMatchRelease': True,
    'validation': 'Compiled and local checks; v2.2 hardware, XP/Win7 and target Android testing pending. See docs/VALIDATION.md.',
    'assets': [{'name': p.name, 'size': p.stat().st_size, 'sha256': digest(p.read_bytes())} for p in assets],
}
manifest_path = out / 'release-manifest.json'
manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
assets.append(manifest_path)
(out / 'SHA256SUMS.txt').write_text(''.join(digest(p.read_bytes()) + '  ' + p.name + '\n' for p in assets), encoding='utf-8')
print(json.dumps({'output': str(out), 'attachments': len(assets) + 1,
                  'embeddedArtifactsMatch': True}, ensure_ascii=False))
