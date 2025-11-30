BUILD_DIR = build
CMAKE_FLAGS = -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

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
	find . -name "*.c" | grep -v build | xargs clang-tidy --fix-errors -p $(BUILD_DIR)/compile_commands.json

lint-check:
	find . -name "*.c" | grep -v build | xargs clang-tidy -p $(BUILD_DIR)/compile_commands.json

help:
	@echo "Available targets:"
	@echo "  build         - Build all firmware (dshot.uf2 and pwm.uf2)"
	@echo "  flash-dshot   - Build and flash dshot firmware"
	@echo "  flash-pwm     - Build and flash pwm firmware"
	@echo "  clean         - Clean build dir"
	@echo "  format        - Format C code"
	@echo "  format-check  - Check C code formatting"
	@echo "  lint          - Lint C code"
	@echo "  lint-check    - Check C code linting"
	@echo "  help          - Show this help"
