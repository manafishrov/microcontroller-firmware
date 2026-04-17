# Microcontroller Firmware

Firmware for the Raspberry Pi Pico and Pico 2 used in the Manafish ROV to
control thrusters. It supports two control protocols:

- DShot (digital ESC control)
- PWM (analog ESC control)

All development dependencies are included in the firmware environment on the
Manafish Pi, so you can develop directly on the device.

## Prerequisites

- Raspberry Pi Pico SDK (automatically fetched by CMake)
- clang-format and clang-tidy
- picotool for flashing
- arm-none-eabi-gcc toolchain
- CMake and Make
- picocom for debugging

If you have **[Nix](https://nixos.org/)** and **[direnv](https://direnv.net/)** installed:

1. Enter the directory: `cd microcontroller-firmware`
2. Run `direnv allow`

This will automatically download and configure the Pico SDK, ARM toolchain,
Clang tools, and CMake.

## Building and Flashing

The project uses CMake to configure the build. The Pico SDK is downloaded
automatically on first build.

### Commands

Run `make help` to list all available targets.

Key targets include:

- `make build-pico` – Build DShot and PWM firmware for Pico
- `make build-pico2` – Build DShot and PWM firmware for Pico 2
- `make flash-dshot-pico` – Build and flash DShot firmware for Pico
- `make flash-pwm-pico` – Build and flash PWM firmware for Pico
- `make flash-dshot-pico2` – Build and flash DShot firmware for Pico 2
- `make flash-pwm-pico2` – Build and flash PWM firmware for Pico 2
- `make clean` – Remove build directories
- `make format` – Format source code
- `make format-check` – Verify formatting (useful for CI)
- `make lint` – Lint and auto-fix C code
- `make lint-check` – Check C code lint

### Build Output

Compiled `.uf2` files appear in:

- `build/pico/dshot.uf2` / `build/pico2/dshot.uf2`
- `build/pico/pwm.uf2` / `build/pico2/pwm.uf2`

### Flashing

Use `make flash-dshot-pico` or `make flash-pwm-pico` (or the `-pico2` variants).
Flashing works regardless of whether the Pico is in BOOTSEL mode—the device
reboots automatically as needed.

## Debugging

The firmware outputs debug messages via USB CDC. Use a serial monitor
like `picocom` to debug:

1. Find the Pico's serial port:

   ```sh
   ls /dev/ttyACM*  # Linux
   ls /dev/tty.usbmodem*  # Darwin
   ```

2. Connect to the device:

   ```sh
   # Linux
   picocom -b 115200 /dev/ttyACM0

   # Darwin
   picocom -b 115200 /dev/tty.usbmodem*
   ```

3. Exit with **Ctrl+A**, then **K** and confirm.

## License

This project is licensed under the GNU Affero General Public License
v3.0 or later - see the [LICENSE](LICENSE) file for details.
