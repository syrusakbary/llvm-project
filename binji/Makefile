clang = ${HOME}/opt/wasi-sdk/bin/clang
ld = ${HOME}/opt/wasi-sdk/bin/wasm-ld
sysroot = ${HOME}/opt/wasi-sdk/share/wasi-sysroot

memfs: memfs.o
	${ld} --no-entry --export-dynamic --allow-undefined -o $@ $<

memfs.o: memfs.c
	${clang} --sysroot=${sysroot} -O2 -Wall -Wextra -Wno-unused-parameter -c -o $@ $<
