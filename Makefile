.DEFAULT_GOAL := help
.PHONY: help embed-html init build clean run test demo

HTML_FILE := ./www/index.html
HEADER_FILE := ./www/index_html.h

help:
	@echo "Available targets:"
	@awk 'BEGIN {FS = ":.*#"} /^[a-zA-Z_-]+:.*?#/ {printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)


embed-html: $(HEADER_FILE) # Embed HTML into header
	@echo "Embedding html..."

$(HEADER_FILE): $(HTML_FILE)
	@rm -f $@
	@echo 'static const char* kStaticHTML = R"rawliteral(' > $@
	@cat $< >> $@
	@echo ')rawliteral";' >> $@
	@echo "Generated $@ from $<"

init: # Init directories
	mkdir -p build

build: init embed-html # Build project
	cd build && cmake .. && cmake --build .

clean: # Clean build files
	rm -rf build

run: build # Run target
	./build/castor

test: build # Run test case
	./build/castor --calendar ./test/calendar/test.csv

demo: build # Run demo
	./build/castor --calendar ./test/calendar/demo.csv
