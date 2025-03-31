This project is built using the GNU ARM compiler toolchain (arm-none-eabi-gcc).

The startup file (startup.c), linker script (TM4C123GH6PM.ld) and all assembly files (*.s) have been modified to be compatible with the GNU toolchain.

The minimum (I believe) required dependencies are:
* arm-none-eabi-gcc
* arm-none-eabi-newlib
* arm-none-eabi-binutils
* lm4flash (or alternatively OpenOCD)

Ideally, the entire project cant be built and flashed by running `make flash` in the root directory.

If you are running this on Windows (and you manage to compile), the `DEV` variable in the Makefile may need to be modified.
