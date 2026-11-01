# SYN-MAIL: make builds into build/, make test runs the stand-in tests (no live mail).
all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build build -j3

test: all
	sh tests/run.sh build/syn-mail

clean:
	rm -rf build

.PHONY: all test clean
