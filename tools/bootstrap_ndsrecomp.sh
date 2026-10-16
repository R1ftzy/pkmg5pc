#!/usr/bin/env sh
# Bootstrap + build the ndsrecomp recompiler + runner (pinned git submodule).
#
# Mirror of tools/bootstrap_ndsrecomp.ps1 for Linux/macOS.
#
# Usage:
#   sh tools/bootstrap_ndsrecomp.sh
#   sh tools/bootstrap_ndsrecomp.sh --build-runner
#   sh tools/bootstrap_ndsrecomp.sh --build-runner --sdl3
set -eu

build_runner=0
sdl3=0
for arg in "$@"; do
  case "$arg" in
    --build-runner) build_runner=1 ;;
    --sdl3) sdl3=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

case "$0" in
  */*) here=$(cd "${0%/*}" && pwd) ;;
  *)   here=$(pwd) ;;
esac
root=$(cd "$here/.." && pwd)
eng="$root/third_party/ndsrecomp"
gen="$eng/generated"

step() { printf '\033[36m==> %s\033[0m\n' "$1"; }

step "Init ndsrecomp submodule"
git -C "$root" submodule update --init -- third_party/ndsrecomp

# The v0.6.10 nested submodule URL is relative and broken; point it at GitHub.
if [ ! -f "$eng/external/arm-recomp-core/CMakeLists.txt" ]; then
  git -C "$eng" config submodule.external/arm-recomp-core.url https://github.com/mstan/arm-recomp-core.git
fi
git -C "$eng" submodule update --init --recursive

step "Stage toml++ single header"
toml="$eng/recompiler/third_party/toml.hpp"
if [ ! -f "$toml" ]; then
  mkdir -p "$(dirname "$toml")"
  tmp=$(mktemp -d)
  git clone --depth 1 --branch v3.4.0 https://github.com/marzer/tomlplusplus.git "$tmp"
  cp "$tmp/toml.hpp" "$toml"
  rm -rf "$tmp"
fi

step "Build recompiler"
cmake -G Ninja -S "$eng/recompiler" -B "$eng/recompiler/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$eng/recompiler/build"

step "Generate BIOS banks"
rc="$eng/recompiler/build/nds_recompile"
f9="$eng/third_party/freebios/drastic_bios_arm9.bin"
f7="$eng/third_party/freebios/drastic_bios_arm7.bin"
c9="$eng/bios/freebios9.toml"
c7="$eng/bios/freebios7.toml"
"$rc" --config "$c9" --bin "$f9" --out "$gen" --bank freebios_arm9
"$rc" --config "$c7" --bin "$f7" --out "$gen" --bank freebios_arm7
# Retail BIOS banks must exist to link, but no retail dump is shipped; emit
# FreeBIOS-content placeholders. The runtime uses the FreeBIOS banks by default.
"$rc" --config "$c9" --bin "$f9" --out "$gen" --bank arm9_bios
"$rc" --config "$c7" --bin "$f7" --out "$gen" --bank arm7_bios

if [ "$build_runner" -eq 1 ]; then
  step "Configure + build runner"
  if [ "$sdl3" -eq 1 ]; then backend=SDL3; compute=ON; else backend=NONE; compute=OFF; fi
  cmake -G Ninja -S "$eng/runner" -B "$eng/runner/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNDS_SDL_BACKEND="$backend" \
    -DNDS_ENABLE_COMPUTE_RENDERER="$compute" \
    -DNDS_BOOTSTRAP_FIRMWARE=ON
  cmake --build "$eng/runner/build"
fi

step "Done"
