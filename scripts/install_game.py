"""Build release LittleFS images or upload Lua games through the experimental USB console."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time



def validate_package(path):
    data = Path(path).read_bytes()
    if not 0 < len(data) <= 131072 or b'\0' in data:
        raise ValueError('Game must be a NUL-free Lua source file of at most 128 KiB')
    first = data.split(b'\n', 1)[0]
    prefix = b'-- pubmote-game '
    if len(first) >= 255 or not first.startswith(prefix):
        raise ValueError('Missing or oversized game metadata header')
    metadata = json.loads(first[len(prefix):])
    if metadata.get('api') != 1 or not re.fullmatch('[a-z0-9_-]{1,23}', metadata.get('id', '')):
        raise ValueError('Unsupported host API or invalid game ID')
    for key, limit in [('title', 47), ('version', 23)]:
        value = metadata.get(key)
        if not isinstance(value, str) or not 0 < len(value.encode()) <= limit or '\0' in value:
            raise ValueError(f'Invalid {key}')
    return metadata, data


def command(port, text, timeout=15):
    port.write((text + '\n').encode())
    port.flush()
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        line = port.readline().decode(errors='replace').strip()
        if line == 'GAME OK':
            return
        if line.startswith('GAME ERROR'):
            raise RuntimeError(line)
    raise TimeoutError(f'No acknowledgement for {text.split()[1]} (upload was not activated)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--build-fs', type=Path, metavar='IMAGE', help='Build a distributable 2 MiB LittleFS image instead of uploading')
    parser.add_argument('--mklittlefs', type=Path, help='Override the PlatformIO mklittlefs executable')
    parser.add_argument('--format', action='store_true', help='ERASE ALL LittleFS content first, including non-game files')
    parser.add_argument('--remove', metavar='GAME_ID')
    parser.add_argument('games', nargs='*', type=Path)
    args = parser.parse_args()
    packages = [validate_package(path) for path in args.games]
    if len({metadata['id'] for metadata, _ in packages}) != len(packages):
        parser.error('Duplicate game IDs')
    if args.build_fs:
        if not packages or args.port or args.remove or args.format:
            parser.error('--build-fs requires game files and cannot be combined with device operations')
        tool = args.mklittlefs or shutil.which('mklittlefs')
        if not tool:
            directory = Path.home() / '.platformio/packages/tool-mklittlefs'
            tool = next((p for p in (directory/'mklittlefs.exe', directory/'mklittlefs') if p.is_file()), None)
        if not tool:
            parser.error('Install PlatformIO mklittlefs or supply --mklittlefs PATH')
        args.build_fs.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory() as directory:
            games = Path(directory) / 'games'
            games.mkdir()
            for metadata, data in packages:
                (games / f'{metadata["id"]}.lua').write_bytes(data)
            subprocess.run([str(tool), '-c', directory, '-b', '4096', '-p', '256',
                            '-s', str(0x200000), str(args.build_fs)], check=True)
        print(f'Built {args.build_fs} with {len(packages)} games')
        return
    if not args.port:
        parser.error('Specify --port for device operations or --build-fs to create an image')
    if args.remove and not re.fullmatch('[a-z0-9_-]{1,23}', args.remove):
        parser.error('Invalid game ID')
    if not packages and not args.remove and not args.format:
        parser.error('Specify game files or --remove')
    import serial

    with serial.Serial(args.port, 115200, timeout=.5) as port:
        time.sleep(2)  # Allow USB console initialization/debounce.
        port.write(b'\n'); port.flush(); time.sleep(.2); port.reset_input_buffer()
        if args.format:
            command(port, 'game format ERASE_LITTLEFS', 60)
        if args.remove:
            command(port, f'game remove {args.remove}')
        for metadata, data in packages:
            digest = hashlib.sha256(data).hexdigest()
            command(port, f'game begin {metadata["id"]} {len(data)} {digest}')
            try:
                # Stay below the USB console's receive buffer, including hex
                # expansion and command text. Larger bursts can lose the newline.
                for offset in range(0, len(data), 64):
                    command(port, f'game chunk {offset} {data[offset:offset+64].hex()}')
                command(port, 'game commit')
            except Exception:
                try:
                    command(port, 'game abort')
                except Exception:
                    pass
                raise
            print(f'Installed {metadata["title"]} {metadata["version"]} ({len(data)} bytes)')
    print('Reopen Arcade to refresh the installed-game list.')


if __name__ == '__main__':
    main()
