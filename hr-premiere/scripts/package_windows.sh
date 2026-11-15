#!/usr/bin/env bash
# Packages HR Premiere for Windows (run inside an MSYS2 shell after building).
#   package_windows.sh <build-dir> <output-dir>
# Result: <output-dir> contains HRPremiere.exe, ffmpeg.exe, the Qt plugins and every DLL
# they need, i.e. a folder that runs on a clean Windows machine.
set -euo pipefail
BUILD="${1:?build dir}"
OUT="${2:?output dir}"

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$BUILD/HRPremiere.exe" "$OUT/"

FFMPEG="$(command -v ffmpeg.exe || command -v ffmpeg)"
echo "bundling ffmpeg from $FFMPEG"
cp "$FFMPEG" "$OUT/ffmpeg.exe"
cp "$(dirname "$0")/../installer/NOTICE.txt" "$OUT/NOTICE.txt"

# 1) Qt plugins and Qt DLLs ---------------------------------------------------------
DEPLOY=""
for name in windeployqt-qt6 windeployqt6 windeployqt; do
  if command -v "$name" >/dev/null 2>&1; then DEPLOY="$name"; break; fi
done
if [ -n "$DEPLOY" ]; then
  echo "running $DEPLOY"
  "$DEPLOY" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$OUT/HRPremiere.exe" || echo "windeployqt failed, falling back to a manual copy"
fi
PLUGINS="$(qtpaths6 --plugin-dir 2>/dev/null || true)"
[ -d "$PLUGINS" ] || PLUGINS="$(dirname "$(command -v qmake6 2>/dev/null || echo /ucrt64/bin/x)")/../share/qt6/plugins"
for d in platforms styles imageformats iconengines multimedia tls generic; do
  if [ -d "$PLUGINS/$d" ] && [ ! -d "$OUT/$d" ]; then cp -r "$PLUGINS/$d" "$OUT/$d"; fi
done

# 1b) whisper.cpp's ggml loads its CPU back-end as a plug-in DLL at run time (ggml-cpu-*.dll, searched next to the
#     program that uses it, here ffmpeg.exe). `ldd` cannot see run-time plug-ins, so they are copied explicitly:
#     without them the Whisper filter aborts with "GGML_ASSERT(device) failed". Only the CPU ones are taken (the GPU
#     back-ends pull in Vulkan / BLAS and the speech filter is run with use_gpu=false anyway).
PREFIX="${MSYSTEM_PREFIX:-$(dirname "$(dirname "$FFMPEG")")}"
echo "---- ggml / whisper in $PREFIX ----"
(pacman -Q 2>/dev/null | grep -i -E 'ggml|whisper|ffmpeg' || true)
find "$PREFIX" \( -iname '*ggml*.dll' -o -iname '*whisper*.dll' \) 2>/dev/null | sort || true
# The "sapphirerapids" variant uses Intel AMX.  ggml picks the best variant from the CPU's feature bits only, but AMX also
# needs the operating system to have enabled it; where it has not (Windows Server, older Windows 10) the speech filter
# dies on the first matrix multiplication.  Without that file ggml simply takes the next best variant (AVX-512 / AVX2).
while IFS= read -r plugin; do
  [ -n "$plugin" ] || continue
  base="$(basename "$plugin")"
  case "$base" in *sapphirerapids*|*amx*) echo "left out (AMX): $base"; continue;; esac
  if [ ! -f "$OUT/$base" ]; then cp "$plugin" "$OUT/$base"; echo "bundled ggml CPU plug-in: $base"; fi
done < <(find "$PREFIX" \( -iname 'ggml-cpu*.dll' -o -iname 'libggml-cpu*.dll' \) 2>/dev/null)

# 2) every DLL that the exe / ffmpeg / plugins depend on (transitively) ----------------
changed=1
while [ "$changed" -eq 1 ]; do
  changed=0
  while IFS= read -r f; do
    while IFS= read -r dll; do
      [ -n "$dll" ] || continue
      base="$(basename "$dll")"
      if [ ! -f "$OUT/$base" ]; then cp "$dll" "$OUT/$base"; changed=1; fi
    done < <(ldd "$f" 2>/dev/null | awk '/=> \//{print $3}' | grep -v -i -E '^/(c|d)/windows|^/windows' || true)
  done < <(find "$OUT" -type f \( -iname '*.dll' -o -iname '*.exe' \))
done

echo "---- package contents ----"
du -sh "$OUT"
ls "$OUT" | head -80
