#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
app="build/Снегопад.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp macos/Info.plist "$app/Contents/Info.plist"
cp macos/Resources/*.png "$app/Contents/Resources/"
xcrun clang -arch arm64 -arch x86_64 -mmacosx-version-min=12.0 -O2 \
    -Wall -Wextra -Werror -Wno-unused-parameter -fobjc-arc -Icore \
    -framework AppKit -framework CoreGraphics -framework QuartzCore core/snow.c macos/main.m \
    -o "$app/Contents/MacOS/SnowOverlay"
if [ -n "${SNOW_SIGNING_IDENTITY:-}" ]; then
    codesign --force --options runtime --timestamp --sign "$SNOW_SIGNING_IDENTITY" "$app"
else
    codesign --force --sign - "$app"
fi
codesign --verify --strict "$app"
printf '%s\n' "$PWD/$app"
