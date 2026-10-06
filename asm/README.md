# RV32I Assembly

This directory contains maintained RV32I assembly sources.  Generated Ripes
sources, objects, and ELF files are written to `build/rv32i/` and are not
committed.

## Sources

| File | Purpose |
|---|---|
| `rv32i_solver_core.s` | Parser, ranking, IDA*, transitions, and replay |
| `rv32i_solver_main.s` | Renderer-free production entry point |
| `rv32i_solver_led_main.s` | LED Matrix production entry point |
| `rv32i_led_renderer.s` | Cube state animation and LED framebuffer output |
| `rv32i_reference_start.s` | Freestanding startup for the GCC C baseline |
| `static_tables_rv32i.inc` | Host-generated transition and heuristic tables |

Assembly test harnesses are in `tests/`.

## Build and test

```bash
make rv32i             # build/rv32i/rv32i_solver.s
make rv32i-led         # build/rv32i/rv32i_solver_led.s
make check-rv32i-asm   # solver tests on RV32_ISS and RV32_5S
make check-rv32i-led   # renderer tests on RV32_ISS and RV32_5S
```

For the GUI visualization, create a 35-by-25 LED Matrix in Ripes before
loading `build/rv32i/rv32i_solver_led.s`.
