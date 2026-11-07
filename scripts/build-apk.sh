#!/usr/bin/env bash
# Builds a signed APK without Gradle or the Google Android SDK, using the Android build tools
# packaged by Debian/Ubuntu:
#
#   sudo apt-get install aapt apksigner zipalign dalvik-exchange android-sdk-platform-23
#
# Resources are linked against Ubuntu's API 23 android.jar. Java is compiled against the
# Android 15 framework jar that Robolectric publishes on Maven Central (downloaded once into
# .cache/ and checked against its published SHA-1), so newer APIs can be used behind
# Build.VERSION checks.
#
# The normal way to build is still Gradle (./gradlew assembleDebug) or Android Studio.
# Versions and the application id are read from app/build.gradle so both builds stay in sync.
#
# Environment overrides:
#   ANDROID_JAR  android.jar to link resources against
#   COMPILE_JAR  framework jar to compile Java against
#   BUILD_TOOLS  directory containing aapt2
#   KEYSTORE     signing keystore (created on first run if missing)
#   KS_PASS      keystore and key password
#   OUT_APK      output path of the final APK
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/app"
OUT="$ROOT/build-nosdk"

ANDROID_JAR="${ANDROID_JAR:-/usr/lib/android-sdk/platforms/android-23/android.jar}"
COMPILE_JAR="${COMPILE_JAR:-$ROOT/.cache/android-all-35.jar}"
ANDROID_ALL_VERSION="15-robolectric-14663667"
BUILD_TOOLS="${BUILD_TOOLS:-/usr/lib/android-sdk/build-tools/debian}"
AAPT2="$BUILD_TOOLS/aapt2"
DX="$(command -v dalvik-exchange || echo "$BUILD_TOOLS/dx")"
KEYSTORE="${KEYSTORE:-$ROOT/.signing/release.keystore}"
KS_PASS="${KS_PASS:-wifiscanner}"
KEY_ALIAS="wifiscanner"

gradle_val() {
    sed -nE "s/^[[:space:]]*$1[[:space:]]*=?[[:space:]]*['\"]?([^'\" ]+)['\"]?.*/\1/p" \
        "$APP/build.gradle" | head -n 1
}

APP_ID="$(gradle_val applicationId)"
MIN_SDK="$(gradle_val minSdk)"
TARGET_SDK="$(gradle_val targetSdk)"
VERSION_CODE="$(gradle_val versionCode)"
VERSION_NAME="$(gradle_val versionName)"
OUT_APK="${OUT_APK:-$OUT/WifiScanner-v$VERSION_NAME.apk}"

echo "==> $APP_ID v$VERSION_NAME ($VERSION_CODE), minSdk $MIN_SDK, targetSdk $TARGET_SDK"

rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/classes" "$OUT/compiled"

echo "==> Compiling resources"
"$AAPT2" compile --dir "$APP/src/main/res" -o "$OUT/compiled/res.zip"

echo "==> Linking resources"
# Gradle injects the package name from `namespace`; do the same here.
sed "s#<manifest #<manifest package=\"$APP_ID\" #" "$APP/src/main/AndroidManifest.xml" \
    > "$OUT/AndroidManifest.xml"
"$AAPT2" link -I "$ANDROID_JAR" \
    --manifest "$OUT/AndroidManifest.xml" \
    --java "$OUT/gen" \
    --min-sdk-version "$MIN_SDK" \
    --target-sdk-version "$TARGET_SDK" \
    --version-code "$VERSION_CODE" \
    --version-name "$VERSION_NAME" \
    -o "$OUT/resources.apk" \
    "$OUT/compiled/res.zip"

if [ ! -f "$COMPILE_JAR" ]; then
    echo "==> Downloading Android framework jar ($ANDROID_ALL_VERSION)"
    url="https://repo1.maven.org/maven2/org/robolectric/android-all/$ANDROID_ALL_VERSION/android-all-$ANDROID_ALL_VERSION.jar"
    mkdir -p "$(dirname "$COMPILE_JAR")"
    curl -fsSL -o "$COMPILE_JAR.part" "$url"
    echo "$(curl -fsSL "$url.sha1")  $COMPILE_JAR.part" | sha1sum -c --quiet -
    mv "$COMPILE_JAR.part" "$COMPILE_JAR"
fi

echo "==> Compiling Java"
find "$APP/src/main/java" "$OUT/gen" -name '*.java' > "$OUT/sources.txt"
# java.* comes from the JDK's Java 8 API (--release 8); android.* from the framework jar.
javac -nowarn -Xlint:-options -encoding UTF-8 --release 8 \
    -classpath "$COMPILE_JAR" \
    -d "$OUT/classes" @"$OUT/sources.txt"

echo "==> Dexing"
"$DX" --dex --min-sdk-version="$MIN_SDK" --output="$OUT/classes.dex" "$OUT/classes"

echo "==> Packaging"
# resources.arsc must be stored uncompressed (required when targetSdk >= 30).
python3 - "$OUT/resources.apk" "$OUT/classes.dex" "$OUT/unaligned.apk" <<'PY'
import sys, zipfile
src, dex, out = sys.argv[1:4]
stamp = (2024, 1, 1, 0, 0, 0)
with zipfile.ZipFile(src) as zin, zipfile.ZipFile(out, "w") as zout:
    for item in zin.infolist():
        info = zipfile.ZipInfo(item.filename, date_time=stamp)
        stored = item.filename == "resources.arsc" or item.compress_type == zipfile.ZIP_STORED
        info.compress_type = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
        zout.writestr(info, zin.read(item.filename))
    info = zipfile.ZipInfo("classes.dex", date_time=stamp)
    info.compress_type = zipfile.ZIP_DEFLATED
    with open(dex, "rb") as f:
        zout.writestr(info, f.read())
PY
zipalign -p -f 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"

echo "==> Signing"
if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkeypair -noprompt -keystore "$KEYSTORE" -storetype PKCS12 \
        -storepass "$KS_PASS" -keypass "$KS_PASS" -alias "$KEY_ALIAS" \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Wifi Scanner, O=Wifi Scanner" >/dev/null
fi
mkdir -p "$(dirname "$OUT_APK")"
KS_PASS="$KS_PASS" apksigner sign --ks "$KEYSTORE" --ks-key-alias "$KEY_ALIAS" \
    --ks-pass env:KS_PASS --key-pass env:KS_PASS \
    --min-sdk-version "$MIN_SDK" \
    --out "$OUT_APK" "$OUT/aligned.apk"
apksigner verify "$OUT_APK"

echo "==> Done: $OUT_APK ($(du -h "$OUT_APK" | cut -f1))"
