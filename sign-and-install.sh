#!/usr/bin/env bash
set -e
BUILD_TOOLS="$ANDROID_SDK_ROOT/build-tools/$(ls $ANDROID_SDK_ROOT/build-tools | sort -V | tail -n1)"
KS_PATH="$1"

keystore_cmd="keytool -genkeypair -v \
  -keystore qgc-release.keystore \
  -alias qgc \
  -keyalg RSA -keysize 2048 -validity 10000
"

if [ ! -f "$KS_PATH" ]; then
  echo "Keystore not found at $KS_PATH. Create one with keytool first."
  echo "You can create it with the following command:"
  echo "$keystore_cmd"
  exit 1
fi

UNSIGNED="android-build-Custom-QGroundControl-release-unsigned.apk"
ALIGNED="android-build-Custom-QGroundControl-release-aligned.apk"
SIGNED="android-build-Custom-QGroundControl-release-signed.apk"

"$BUILD_TOOLS/zipalign" -v -p 4 "$UNSIGNED" "$ALIGNED"
"$BUILD_TOOLS/apksigner" sign --ks "$KS_PATH" --ks-key-alias qgc --out "$SIGNED" "$ALIGNED"
"$BUILD_TOOLS/apksigner" verify --print-certs "$SIGNED"
adb install -r "$SIGNED"
