#!/bin/sh
# Builds blink.wasm and blink.wasm.h (committed, so the mini OS builds without a WebAssembly compiler).
set -e
cd "$(dirname "$0")"
CLANG="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
"$CLANG" --target=wasm32 -nostdlib -O2 -I../../include -Wl,--no-entry -Wl,--export=app_main \
    -Wl,-z,stack-size=256 -Wl,--page-size=1 -o blink.wasm blink.c
# Read-only bytes; BLINK_SECTION puts them in flash on AVR (PROGMEM), as WM_PROGRAM_IN_FLASH reads them.
xxd -i blink.wasm | sed 's/^unsigned char blink_wasm\[\] = {/static const unsigned char blink_wasm[] BLINK_SECTION = {/; s/^unsigned int /static const unsigned int /' > blink.wasm.h
wc -c blink.wasm
