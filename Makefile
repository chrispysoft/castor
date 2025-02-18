.DEFAULT_GOAL := help

help:
	@echo "Available targets:"
	@awk 'BEGIN {FS = ":.*#"} /^[a-zA-Z_-]+:.*?#/ {printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)


init: # Init directories
	mkdir -p build

build: init # Build project
	cd build && cmake .. && cmake --build .

clean: # Clean build files
	rm -rf build

run: build # Run target
	./build/castor

test: build # Run test case
	./build/castor --calendar ./test/calendar/test.csv

demo: build # Run demo
	./build/castor --calendar ./test/calendar/demo.csv
