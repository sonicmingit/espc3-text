"""Flash the four release images without erasing NVS or LittleFS."""
import argparse
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True, help='Actual ESP32-C3 serial port, e.g. COM5')
parser.add_argument('--baud', type=int, default=460800)
args = parser.parse_args()
images = [('0x0', 'bootloader.bin'), ('0x8000', 'partitions.bin'),
          ('0xe000', 'boot_app0.bin'), ('0x10000', 'firmware.bin')]
for _, name in images:
    if not (root / name).is_file():
        raise SystemExit('Missing ' + name + '; extract the firmware release ZIP first.')
cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', args.port,
       '--baud', str(args.baud), 'write_flash', '--flash_mode', 'dio',
       '--flash_freq', '40m', '--flash_size', '4MB']
for address, name in images:
    cmd.extend([address, str(root / name)])
subprocess.run(cmd, check=True)
