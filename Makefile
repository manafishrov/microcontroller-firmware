BUILD_DIR = build
CMAKE_FLAGS = -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ARM_GCC_INCLUDE = $(shell arm-none-eabi-gcc -print-file-name=include)
SYSROOT_A = $(shell arm-none-eabi-gcc -print-sysroot)/include
SYSROOT_B = /usr/arm-none-eabi/include
SYSROOT_C = /usr/lib/arm-none-eabi/include

.PHONY: build flash-dshot flash-pwm clean format format-check lint lint-check help

build:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)

flash-dshot: build
	picotool load $(BUILD_DIR)/src/dshot/dshot.uf2 -f

flash-pwm: build
	picotool load $(BUILD_DIR)/src/pwm/pwm.uf2 -f

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
	@echo "  build         - Build all firmware (dshot.uf2 and pwm.uf2)"
	@echo "  flash-dshot   - Build and flash dshot firmware"
	@echo "  flash-pwm     - Build and flash pwm firmware"
	@echo "  clean         - Clean build dir"
	@echo "  format        - Format C code"
	@echo "  format-check  - Check C code formatting"
	@echo "  lint          - Lint C code (auto-fix errors)"
	@echo "  lint-check    - Check C code linting (report only)"
	@echo "  help          - Show this help"
