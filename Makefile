BUILD_DIR = build
CMAKE_FLAGS = -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
CMAKE_FLAGS_PICO2 = $(CMAKE_FLAGS) -DPICO_PLATFORM=rp2350-arm-s -DPICO_BOARD=pico2
ARM_GCC_INCLUDE = $(shell arm-none-eabi-gcc -print-file-name=include)
SYSROOT_A = $(shell arm-none-eabi-gcc -print-sysroot)/include
SYSROOT_B = /usr/arm-none-eabi/include
SYSROOT_C = /usr/lib/arm-none-eabi/include

.PHONY: build-pico build-pico2 flash-dshot-pico flash-pwm-pico flash-dshot-pico2 flash-pwm-pico2 clean format format-check lint lint-check help

build-pico:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)

build-pico2:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. $(CMAKE_FLAGS_PICO2)
	cmake --build $(BUILD_DIR)

flash-dshot-pico: build-pico
	picotool load $(BUILD_DIR)/dshot-pico.uf2 -f

flash-pwm-pico: build-pico
	picotool load $(BUILD_DIR)/pwm-pico.uf2 -f

flash-dshot-pico2: build-pico2
	picotool load $(BUILD_DIR)/dshot-pico2.uf2 -f

flash-pwm-pico2: build-pico2
	picotool load $(BUILD_DIR)/pwm-pico2.uf2 -f

clean:
	rm -rf $(BUILD_DIR)

format:
	find . -name "*.c" -o -name "*.h" | grep -v build | xargs clang-format -i

format-check:
	find . -name "*.c" -o -name "*.h" | grep -v build | xargs clang-format --dry-run --Werror

lint:
	cmake --build $(BUILD_DIR)
	find . -name "*.c" | grep -v build | xargs clang-tidy --fix-errors \
	-p $(BUILD_DIR)/compile_commands.json \
	-header-filter="^$(CURDIR)/src/.*" \
	--extra-arg=-I$(ARM_GCC_INCLUDE) \
	--extra-arg=-I$(SYSROOT_A) \
	--extra-arg=-I$(SYSROOT_B) \
	--extra-arg=-I$(SYSROOT_C)

lint-check:
	cmake --build $(BUILD_DIR)
	find . -name "*.c" | grep -v build | xargs clang-tidy \
	-p $(BUILD_DIR)/compile_commands.json \
	-header-filter="^$(CURDIR)/src/.*" \
	--extra-arg=-I$(ARM_GCC_INCLUDE) \
	--extra-arg=-I$(SYSROOT_A) \
	--extra-arg=-I$(SYSROOT_B) \
	--extra-arg=-I$(SYSROOT_C)

help:
	@echo "Available targets:"
	@echo "  build-pico      - Build all firmware for Pico (pwm/pico.uf2 and dshot/pico.uf2)"
	@echo "  build-pico2     - Build all firmware for Pico 2 (pwm/pico.uf2 and dshot/pico.uf2)"
	@echo "  flash-dshot-pico   - Build and flash dshot firmware for Pico"
	@echo "  flash-pwm-pico     - Build and flash pwm firmware for Pico"
	@echo "  flash-dshot-pico2  - Build and flash dshot firmware for Pico 2"
	@echo "  flash-pwm-pico2    - Build and flash pwm firmware for Pico 2"
	@echo "  clean           - Clean build dirs"
	@echo "  format          - Format C code"
	@echo "  format-check    - Check C code formatting"
	@echo "  lint            - Lint C code (auto-fix errors)"
	@echo "  lint-check      - Check C code linting (report only)"
	@echo "  help            - Show this help"
