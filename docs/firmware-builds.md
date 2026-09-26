# Firmware builds

Build just the board you are working on:

```sh
pio run -e pingumote_esp32s3_touch_amoled_132
```

A plain `pio run` builds all four default environments. Normal builds produce
`.pio/build/<environment>/firmware.bin` and retain the objects for incremental
builds. There is no need to clean before editing, building or uploading.

Release packaging is a separate target:

```sh
pio run -e pingumote_esp32s3_touch_amoled_132 -t package
```

This builds the firmware and writes the versioned ZIP, BIN and ELF in the project
root. The nightly and release workflows use this target. Uploading remains
`pio run -e <environment> -t upload`.

## What is cached

- PlatformIO objects are cached in `.pio/cache/objects`. Matching objects can be
  restored after a clean or a configuration change; incompatible compiler flags,
  sources or headers get new objects.
- Slint release downloads are cached in `.pio/cache/slint`, outside the directories
  removed by `pio run -t clean`. Each release/host/asset combination has its own key.
- Generated UI files live in `.pio/build/<environment>/slint_generated`, so boards
  with different font sizes do not overwrite each other's output. Generation
  tracks Slint sources, assets, compiler and font settings and preserves unchanged
  output files. Private resource declarations are kept separate from the public UI
  header so varying font-resource ordering does not invalidate handwritten code.
- Version/build metadata lives in a separate header. A daily build-ID change only
  invalidates its consumers, not the framework or the generated UI.

## Generated UI compilation

`custom_slint_cpp_files = 8` splits Slint's generated implementation into independent
translation units for parallel compilation. The Slint runtime itself remains a
prebuilt library. The generated code retains the normal release optimization level.

Generated UI sources use `-g1` to retain function/line information without the large
variable/type debug tables. Handwritten sources keep their normal debug settings.
If you need full debugger inspection inside generated code, add
`-DSLINT_FULL_DEBUG=ON` to `board_build.cmake_extra_args`.

## Measuring changes

```sh
python scripts/benchmark_build.py noop
python scripts/benchmark_build.py c-edit --touch-source firmware/src/remote/console.c --replace PUBREMOTE-CONSOLE PUBREMOTE-CONSOLE-BENCH
```

The script stores logs, elapsed times, compile counts and phase timestamps under
`.pio/benchmarks`. It restores a temporary source edit after the build; run another
build afterward to restore the corresponding firmware artifact. Use an isolated
checkout when measuring while an editor may reconfigure or build the project.
