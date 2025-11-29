BUILD_DIR = build
CMAKE_FLAGS = -DCMAKE_BUILD_TYPE=Release

.PHONY: build flash-dshot flash-pwm clean format lint help

build:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)

flash-dshot: build
	picotool load $(BUILD_DIR)/src-dshot/dshot.uf2 -f

flash-pwm: build
	picotool load $(BUILD_DIR)/src-pwm/pwm.uf2 -f

clean:
	rm -rf $(BUILD_DIR)

format:
	find . -name "*.c" -o -name "*.h" | xargs clang-format -i

lint:
	find . -name "*.c" | xargs clang-tidy

help:
	@echo "Available targets:"
	@echo "  build         - Build all firmware (dshot.uf2 and pwm.uf2)"
	@echo "  flash-dshot   - Build and flash dshot firmware"
	@echo "  flash-pwm     - Build and flash pwm firmware"
	@echo "  clean         - Clean build dir"
	@echo "  format        - Format C code"
	@echo "  lint          - Lint C code"
	@echo "  help          - Show this help"
