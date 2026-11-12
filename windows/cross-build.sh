#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
project_dir="$(pwd -P)"
build="$project_dir/build/windows"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-$build/zig-local}"
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$build/zig-global}"
zig="${ZIG:-zig}"
mkdir -p "$build" "$ZIG_LOCAL_CACHE_DIR" "$ZIG_GLOBAL_CACHE_DIR"
cd "$project_dir/windows"
"$zig" rc "/fo$build/SnowOverlay.res" SnowOverlay.rc
"$zig" cc -target x86_64-windows-gnu -std=c17 -O2 -Wall -Wextra -Werror -I../core \
    -c main.c -o "$build/main.obj"
"$zig" cc -target x86_64-windows-gnu -std=c17 -O2 -Wall -Wextra -Werror \
    -c ../core/snow.c -o "$build/snow.obj"
"$zig" cc -target x86_64-windows-gnu -municode -s -Wl,--subsystem,windows \
    "$build/main.obj" "$build/snow.obj" "$build/SnowOverlay.res" \
    -luser32 -lgdi32 -lshell32 -ldwmapi -o "$build/SnowOverlay.exe"
printf '%s\n' "$build/SnowOverlay.exe"
