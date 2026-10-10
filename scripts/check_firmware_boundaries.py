#!/usr/bin/env python3
"""Check first-party firmware component dependencies without requiring ESP-IDF.

Public headers may include other public headers only through REQUIRES. Sources
may also use PRIV_REQUIRES. Extracted modules must never include the application
or another module's implementation. SDK and vendored headers are left to CMake.
"""
from pathlib import Path
import re
import sys

SOURCE_SUFFIXES = {'.c', '.cpp', '.h', '.hpp'}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)


def dependencies(cmake, keyword, modules):
    match = re.search(r'\b' + keyword + r'\s+([^)]*)', cmake)
    if not match:
        return set()
    values = re.split(r'\b(?:SRCS|INCLUDE_DIRS|PRIV_INCLUDE_DIRS|REQUIRES|PRIV_REQUIRES)\b', match[1])[0]
    return set(re.findall(r'\b[A-Za-z_]\w*\b', values)) & set(modules)


def check(root):
    module_root = root / 'firmware/modules'
    modules = {path.name: path for path in module_root.iterdir() if path.is_dir()}
    errors = []
    headers = {}
    graph = {}
    private_graph = {}
    for name, module in modules.items():
        cmake_path = module / 'CMakeLists.txt'
        if not cmake_path.exists():
            errors.append(f'{name}: missing CMakeLists.txt')
            continue
        cmake = cmake_path.read_text(encoding='utf-8')
        graph[name] = dependencies(cmake, 'REQUIRES', modules)
        private_graph[name] = dependencies(cmake, 'PRIV_REQUIRES', modules)
        for dependency in graph[name] | private_graph[name]:
            if dependency not in modules:
                errors.append(f'{name}: unknown component {dependency}')
        for path in (module / 'include').rglob('*'):
            if path.suffix in SOURCE_SUFFIXES:
                spelling = path.relative_to(module / 'include').as_posix()
                if spelling in headers:
                    errors.append(f'{name}: duplicate public header {spelling}')
                headers[spelling] = (name, path)
        registered = set(re.findall(r'"(src/[^"\n]+\.(?:c|cpp))"', cmake))
        actual = {path.relative_to(module).as_posix() for path in (module / 'src').rglob('*')
                  if path.suffix in {'.c', '.cpp'}}
        if registered != actual:
            errors.append(f'{name}: CMake source list differs: {sorted(registered ^ actual)}')
        if re.search(r'(?:INCLUDE_DIRS|REQUIRES)[^)]*(?:firmware/src|\.\./\.\./src)', cmake):
            errors.append(f'{name}: application include directory is forbidden')
        if not (module / 'README.md').exists():
            errors.append(f'{name}: missing ownership/API README.md')

    # Cycles in private dependencies are also architectural cycles.
    visited = set()
    active = []

    def visit(name):
        if name in active:
            errors.append('component cycle: ' + ' -> '.join(active + [name]))
            return
        if name in visited:
            return
        active.append(name)
        for dependency in sorted(graph.get(name, set()) | private_graph.get(name, set())):
            visit(dependency)
        active.pop()
        visited.add(name)

    for name in sorted(modules):
        visit(name)

    app = root / 'firmware/src'
    app_manifest = app / 'sources.cmake'
    if not app_manifest.exists():
        errors.append('application: missing sources.cmake')
    else:
        registered = set(re.findall(r'"([^"\n]+\.(?:c|cpp))"', app_manifest.read_text(encoding='utf-8')))
        actual = {path.relative_to(app).as_posix() for path in app.rglob('*')
                  if path.suffix in {'.c', '.cpp'} and 'generated' not in path.relative_to(app).parts}
        if registered != actual:
            errors.append(f'application: CMake source list differs: {sorted(registered ^ actual)}')
    for name, module in modules.items():
        for path in module.rglob('*'):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            public = 'include' in path.relative_to(module).parts
            allowed = graph.get(name, set()) | ({name} if public else private_graph.get(name, set()) | {name})
            for spelling in INCLUDE.findall(path.read_text(encoding='utf-8')):
                label = f'{path.relative_to(root).as_posix()}: {spelling}'
                candidate = (path.parent / spelling).resolve()
                if candidate.is_file():
                    if candidate.is_relative_to(module):
                        if public and not candidate.is_relative_to(module / 'include'):
                            errors.append(label + ': public header includes implementation')
                        continue
                    errors.append(label + ': include escapes component')
                    continue
                if spelling in headers:
                    owner, _ = headers[spelling]
                    if owner not in allowed:
                        errors.append(label + f': undeclared dependency on {owner}')
                    continue
                if (app / spelling).is_file() or any((directory / spelling).is_file() for directory in app.iterdir() if directory.is_dir()):
                    errors.append(label + ': extracted component includes application')
                if '/src/' in spelling or spelling.startswith('../'):
                    errors.append(label + ': implementation-path include is forbidden')

    # State is private to the owner. Keep generated Slint properties unaffected.
    connection_owner = app / 'remote/connection.c'
    for path in app.rglob('*'):
        if path.suffix not in SOURCE_SUFFIXES or path == connection_owner or 'generated' in path.relative_to(app).parts:
            continue
        content = path.read_text(encoding='utf-8')
        relative = path.relative_to(app).as_posix()
        ui_owner = relative.startswith(('ui/', 'screens/')) or relative in {
            'display/slint_display.cpp', 'utilities/ui_operation.cpp'
        }
        includes = INCLUDE.findall(content)
        if not ui_owner and (re.search(r'\bslint::|\bget_slint_window\s*\(', content) or
                             any(header in {'slint.h', 'slint-esp.h', 'window.h', 'ui/slint_window.h'} or
                                 header.startswith('slint_generated/') for header in includes)):
            errors.append(f'{path.relative_to(root)}: Slint access belongs to UI adapters')
        if relative.startswith('remote/') and any(header.startswith('screens/') for header in includes):
            errors.append(f'{path.relative_to(root)}: workers must use UI adapters, not screen implementations')
        if relative in {'display/panel.cpp', 'display/panel.h'} and any(
                header.startswith(('ui/', 'screens/')) for header in includes):
            errors.append(f'{path.relative_to(root)}: panel hardware cannot depend on presentation')
        if relative == 'display/slint_display.cpp' and any(header.startswith('screens/') for header in includes):
            errors.append(f'{path.relative_to(root)}: renderer must delegate screen lifecycle to navigation')
        if re.search(r'\bextern\s+(?:ConnectionState|PairingState)\b', content):
            errors.append(f'{path.relative_to(root)}: connection state must remain private')
        if re.search(r'\b(?:connection_state|pairing_state)\s*(?:==|!=|=)', content):
            errors.append(f'{path.relative_to(root)}: use the connection state API')
    for base in [app, module_root]:
        for path in base.rglob('*.h'):
            content = path.read_text(encoding='utf-8')
            if re.search(r'\bextern\s+(?:RemoteStats|DeviceSettings|CalibrationSettings|InputPinSettings|ImuCalibrationSettings|PairingSettings)\b', content):
                errors.append(f'{path.relative_to(root)}: settings/telemetry state must remain private')
    return errors


if __name__ == '__main__':
    problems = check(Path(__file__).resolve().parents[1])
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        sys.exit(1)
    print('Firmware component boundaries: OK')
