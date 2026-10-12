"""Compile the production input router with deterministic clock/settings fakes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InputRouterTests(unittest.TestCase):
    def test_neutral_handoff_and_navigation_repeat(self):
        cmake = shutil.which('cmake')
        if not cmake:
            bundled = Path.home() / '.platformio/packages/tool-cmake/bin/cmake.exe'
            if bundled.exists():
                cmake = str(bundled)
        self.assertIsNotNone(cmake, 'Install CMake and a host C11 compiler')
        with tempfile.TemporaryDirectory(prefix='pubremote-input-') as directory:
            staged = Path(directory)
            for name in ('input_router.c', 'input_router.h'):
                shutil.copyfile(ROOT / 'firmware/src/remote' / name, staged / name)
            shutil.copyfile(ROOT / 'tests/input_router_test.c', staged / 'test.c')
            (staged / 'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
            (staged / 'settings.h').write_text('#include <stdbool.h>\nbool is_pocket_mode_enabled(void);\n')
            (staged / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
project(input_router_test C)
add_executable(input_router_test input_router.c test.c)
set_target_properties(input_router_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
if(MSVC)
  target_compile_options(input_router_test PRIVATE /UNDEBUG /W4 /WX)
else()
  target_compile_options(input_router_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
endif()
enable_testing()
add_test(NAME input_router COMMAND input_router_test)
''')
            for command in (
                [cmake, '-S', str(staged), '-B', str(staged / 'build')],
                [cmake, '--build', str(staged / 'build'), '--config', 'Debug'],
                [str(Path(cmake).with_name('ctest.exe' if Path(cmake).suffix == '.exe' else 'ctest')),
                 '--test-dir', str(staged / 'build'), '-C', 'Debug', '--output-on-failure'],
            ):
                result = subprocess.run(command, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
