"""Ensure CI rejects dependency leaks and private-state regressions."""
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_firmware_boundaries import check


class FirmwareBoundariesTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='pubmote-boundaries-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        shutil.copytree(ROOT / 'firmware/modules', self.root / 'firmware/modules')
        for path in (ROOT / 'firmware/src').rglob('*'):
            if path.suffix in {'.c', '.cpp', '.h', '.hpp', '.cmake'}:
                destination = self.root / path.relative_to(ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)

    def append(self, relative, content):
        with (self.root / relative).open('a', encoding='utf-8') as output:
            output.write('\n' + content + '\n')

    def test_current_architecture_passes(self):
        self.assertEqual(check(self.root), [])

    def test_driver_cannot_reach_application_settings(self):
        self.append('firmware/modules/transport/src/espnow.c', '#include "remote/settings.h"')
        self.assertTrue(any('includes application' in error for error in check(self.root)))

    def test_public_header_cannot_leak_private_dependency(self):
        self.append('firmware/modules/transport/include/remote/comms.h', '#include "utilities/diagnostic_log.h"')
        self.assertTrue(any('undeclared dependency on support' in error for error in check(self.root)))

    def test_component_cannot_include_another_implementation(self):
        self.append('firmware/modules/transport/src/comms.c', '#include "../../support/src/log_ring.c"')
        self.assertTrue(any('escapes component' in error for error in check(self.root)))

    def test_unregistered_source_is_rejected(self):
        self.append('firmware/modules/transport/src/accidental.c', 'void accidental(void) {}')
        self.assertTrue(any('CMake source list differs' in error for error in check(self.root)))

    def test_unregistered_application_source_is_rejected(self):
        self.append('firmware/src/remote/accidental.c', 'void accidental(void) {}')
        self.assertTrue(any('application: CMake source list differs' in error for error in check(self.root)))

    def test_dependency_cycle_is_rejected(self):
        path = self.root / 'firmware/modules/support/CMakeLists.txt'
        path.write_text(path.read_text(encoding='utf-8').replace('REQUIRES config',
                        'REQUIRES transport config'), encoding='utf-8')
        self.assertTrue(any('component cycle' in error for error in check(self.root)))

    def test_connection_globals_cannot_return(self):
        self.append('firmware/src/remote/connection.h', 'extern ConnectionState connection_state;')
        self.assertTrue(any('state must remain private' in error for error in check(self.root)))

    def test_settings_globals_cannot_return(self):
        self.append('firmware/src/remote/settings.h', 'extern DeviceSettings device_settings;')
        self.assertTrue(any('settings/telemetry state must remain private' in error for error in check(self.root)))

    def test_telemetry_globals_cannot_return(self):
        self.append('firmware/modules/telemetry/include/remote/stats.h', 'extern RemoteStats remoteStats;')
        self.assertTrue(any('settings/telemetry state must remain private' in error for error in check(self.root)))

    def test_pairing_cannot_reach_slint(self):
        self.append('firmware/src/remote/pairing.c', '#include "ui/slint_window.h"')
        self.assertTrue(any('Slint access belongs to UI adapters' in error for error in check(self.root)))

    def test_worker_cannot_include_generated_slint(self):
        self.append('firmware/src/remote/pairing.c', '#include "slint_generated/app-window.h"')
        self.assertTrue(any('Slint access belongs to UI adapters' in error for error in check(self.root)))

    def test_worker_cannot_reach_screen_implementation(self):
        self.append('firmware/src/remote/receiver.c', '#include "screens/pairing_screen.h"')
        self.assertTrue(any('workers must use UI adapters' in error for error in check(self.root)))

    def test_panel_cannot_reach_presentation(self):
        self.append('firmware/src/display/panel.cpp', '#include "ui/navigation.h"')
        self.assertTrue(any('panel hardware cannot depend on presentation' in error for error in check(self.root)))

    def test_renderer_cannot_own_screen_lifecycle(self):
        self.append('firmware/src/display/slint_display.cpp', '#include "screens/menu_screen.h"')
        self.assertTrue(any('renderer must delegate' in error for error in check(self.root)))


if __name__ == '__main__':
    unittest.main()
