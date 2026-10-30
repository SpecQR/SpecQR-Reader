#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mode="${1:-wasm}"
out="${2:-$root/browser/build}"
case "$mode" in
  wasm|wasm-contract)
    : "${EMXX:?Set EMXX to the absolute path of the pinned em++ compiler}"
    expected_version="$(cat "$root/browser/emscripten-version.txt")"
    version_output="$("$EMXX" --version)"
    version_line="${version_output%%$'\n'*}"
    if [[ ! "$version_line" =~ \ ([0-9]+\.[0-9]+\.[0-9]+)\ \( ]] || [[ "${BASH_REMATCH[1]}" != "$expected_version" ]]; then
      printf 'Emscripten %s is required; got: %s\n' "$expected_version" "$version_line" >&2
      exit 2
    fi
    printf '%s\n' "$version_line"
    ;;
  native|contract) ;;
  *) printf '%s\n' 'usage: build.sh native|contract|wasm-contract|wasm OUTPUT_DIRECTORY' >&2; exit 2 ;;
esac
mkdir -p "$out"
sources=("$root/browser/bridge.cpp" "$root/src/grid.cpp" "$root/src/rs.cpp" "$root/src/payload.cpp" "$root/src/image.cpp" "$root/src/common.cpp")
common=(-std=c++17 -O2 -fexceptions -Wall -Wextra -I"$root/include")
case "$mode" in
  native)
    "${CXX:-c++}" "${common[@]}" "${sources[@]}" "$root/browser/native_driver.cpp" -o "$out/native-bridge"
    ;;
  contract)
    "${CXX:-c++}" "${common[@]}" "${sources[@]}" "$root/browser/contract_tests.cpp" -o "$out/bridge-contract"
    ;;
  wasm-contract)
    : "${EMXX:?Set EMXX to the absolute path of the task-local em++ compiler}"
    # A separate test executable; embedded filesystem and operator-new failure
    # injection are never linked into the distributable browser Reader module.
    "$EMXX" "${common[@]}" "${sources[@]}" "$root/browser/contract_tests.cpp" \
      -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 \
      -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=536870912 -sSTACK_SIZE=1048576 \
      -sABORTING_MALLOC=0 -sDISABLE_EXCEPTION_CATCHING=0 -sASSERTIONS=1 -sEXIT_RUNTIME=1 \
      --embed-file "$root/fixtures/grids/segno-v01-H-m0.grid@/fixture.grid" \
      -o "$out/wasm-contract.cjs"
    ;;
  wasm)
    : "${EMXX:?Set EMXX to the absolute path of the task-local em++ compiler}"
    "$EMXX" "${common[@]}" "${sources[@]}" --no-entry \
      -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createSpecQRModule \
      -sENVIRONMENT=web,worker,node -sFILESYSTEM=0 \
      -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=536870912 \
      -sSTACK_SIZE=1048576 -sABORTING_MALLOC=0 -sDISABLE_EXCEPTION_CATCHING=0 -sASSERTIONS=1 \
      '-sEXPORTED_FUNCTIONS=["_specqr_alloc","_specqr_free","_specqr_live_input_bytes","_specqr_decode","_specqr_result_size","_specqr_release_result"]' \
      '-sEXPORTED_RUNTIME_METHODS=["UTF8ToString","HEAPU8"]' \
      -o "$out/specqr.mjs"
    ;;
  *) printf '%s\n' 'usage: build.sh native|contract|wasm-contract|wasm OUTPUT_DIRECTORY' >&2; exit 2 ;;
esac
