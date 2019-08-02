clang = ~/opt/wasi-sdk/bin/clang
ld = ~/opt/wasi-sdk/bin/wasm-ld

memfs: memfs.o
	${ld} --no-entry --export-dynamic --allow-undefined -o $@ $<

memfs.o: memfs.c
	${clang} -O2 -Wall -Wextra -Wno-unused-parameter -c -o $@ $<
