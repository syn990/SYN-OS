# SYN-MAIL: make builds into build/, make test runs the stand-in tests (no live mail): the desktop client, then the
# phone app's mail core on the desktop JVM (apk/test.sh), which imports the accounts the desktop exported.
# make apk builds the phone app (apk/build.sh); make apk-standin STANDIN=DIR the variant that only talks
# to the stand-in running in DIR (a tests/standin.py folder, reached from the phone over adb reverse).
all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build build -j3

test: all
	rm -rf build/exports && mkdir -m 700 build/exports
	SYN_MAIL_EXPORT_OUT=build/exports sh tests/run.sh build/syn-mail; r=$$?; \
	sh apk/test.sh build/exports; a=$$?; rm -rf build/exports; [ $$r -eq 0 ] && [ $$a -eq 0 ]

apk:
	sh apk/build.sh

apk-standin:
	@[ -n "$(STANDIN)" ] || { echo 'usage: make apk-standin STANDIN=DIR (a running tests/standin.py folder)'; exit 1; }
	sh apk/build.sh --standin "$(STANDIN)"

clean:
	rm -rf build apk/build

.PHONY: all test apk apk-standin clean
