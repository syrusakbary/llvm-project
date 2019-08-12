clang = ${HOME}/opt/wasi-sdk/bin/clang
ld = ${HOME}/opt/wasi-sdk/bin/wasm-ld
sysroot = ${HOME}/opt/wasi-sdk/share/wasi-sysroot

memfs: memfs.o stb_sprintf.o
	${ld} -L${sysroot}/lib/wasm32-wasi --no-entry --export-dynamic --allow-undefined -o $@ $^ -lc

memfs.o: memfs.c
	${clang} --sysroot=${sysroot} -O2 -Wall -Wextra -Wno-unused-parameter -c -o $@ $<

stb_sprintf.o: stb_sprintf.h
	${clang} --sysroot=${sysroot} -DSTB_SPRINTF_IMPLEMENTATION -x c -O2 -c -o $@ $<
