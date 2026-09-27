# DMA accumulator regression test

Run from the repository root with a C++20 compiler and CMake:

```sh
cmake -S tests/dma -B .pio/dma-tests
cmake --build .pio/dma-tests --config Release
ctest --test-dir .pio/dma-tests -C Release --output-on-failure
```

CMake extracts the accumulator block from `slint-esp.cpp`; the test runs that
production code with a synthetic line renderer and display-transfer queue.
It checks 32,000 cases covering overlapping dirty rectangles, skipped bands,
empty/full frames, narrow spans, byte swapping, synchronous transfers, and
asynchronous transfers completing at different points during rendering.

An independent expected image checks every pixel and write count. Additional
checks reject transfers outside the display or allocation, writes to a buffer
still owned by DMA, and odd address windows in the asynchronous AMOLED path.
The dirty input is the union of at most three even-aligned rectangles, matching
Slint's `PhysicalRegion` contract for this board.

This validates buffer contents and ownership. It does not emulate panel scanout,
electrical behavior, interrupts, or wall-clock performance; hardware benchmarks
and visual checks remain necessary.
