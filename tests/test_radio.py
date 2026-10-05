"""Execute production radio functions against deterministic host-side fakes.

Only the selected functions are staged, unchanged, to avoid emulating unrelated
IDF startup and GATT declarations. The firmware build checks their integration.
Diagnostics compiles its full production implementation.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    # Skip a forward declaration with the same signature.
    while source.find(';', start, brace) != -1:
        start = source.index(signature, start + len(signature))
        brace = source.index('{', start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError(f'Unterminated function: {signature}')


class RadioTests(unittest.TestCase):
    def test_radio_failure_paths(self):
        cmake = shutil.which('cmake')
        if not cmake:
            bundled = Path.home() / '.platformio/packages/tool-cmake/bin/cmake.exe'
            if bundled.exists():
                cmake = str(bundled)
        self.assertIsNotNone(cmake, 'Install CMake and a host C11 compiler to run radio tests')
        with tempfile.TemporaryDirectory(prefix='pubmote-radio-') as directory:
            temporary = Path(directory)
            for name, signatures in {
                'ble': ['static bool current_session(', 'static int ble_on_write_cccd(',
                        'static esp_err_t ble_driver_deinit(', 'static esp_err_t ble_send_locked(',
                        'static esp_err_t ble_driver_send(const uint8_t *peer_mac, const uint8_t *data, size_t len) {'],
                'wifi': ['esp_err_t wifi_scan_networks(', 'static esp_err_t connect_to_network_locked(',
                         'esp_err_t wifi_connect_to_network_cancellable('],
                'radio_session': ['static esp_err_t stop_workers(', 'static esp_err_t start_workers(', 'static esp_err_t begin_session(',
                                  'static esp_err_t end_session('],
            }.items():
                path = ROOT / 'firmware/src/remote' / ('comms_ble.c' if name == 'ble' else f'{name}.c')
                source = path.read_text(encoding='utf-8')
                (temporary / f'{name}_functions.inc').write_text(
                    '\n\n'.join(function(source, signature) for signature in signatures), encoding='utf-8')
            comms = (ROOT / 'firmware/src/remote/comms.c').read_text(encoding='utf-8')
            (temporary / 'comms_functions.inc').write_text(function(comms, 'esp_err_t comms_select_driver('), encoding='utf-8')
            lifecycle = (ROOT / 'firmware/src/remote/radio_session.c').read_text(encoding='utf-8')
            (temporary / 'radio_reset_functions.inc').write_text(
                '\n\n'.join(function(lifecycle, signature) for signature in [
                    'static esp_err_t stop_workers(', 'static esp_err_t start_workers(',
                    'esp_err_t radio_session_reset_settings(']), encoding='utf-8')
            commands = [
                [cmake, '-S', str(ROOT / 'tests/radio'), '-B', str(temporary / 'build'),
                 f'-DGENERATED_DIR={temporary.as_posix()}'],
                [cmake, '--build', str(temporary / 'build'), '--config', 'Debug'],
                [str(Path(cmake).with_name('ctest.exe' if os.name == 'nt' else 'ctest')),
                 '--test-dir', str(temporary / 'build'), '-C', 'Debug', '--output-on-failure'],
            ]
            for command in commands:
                result = subprocess.run(command, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
