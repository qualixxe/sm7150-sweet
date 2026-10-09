#!/bin/bash
# Fetch the submodules this build needs.
#
# Using `submodules: recursive` on the checkout step fails. edk2 carries a
# host-based unit test framework whose SubhookLib submodule points at
# github.com/Zeex/subhook, a repository that no longer exists, so the recursion
# aborts:
#
#   remote: Repository not found.
#   fatal: repository 'https://github.com/Zeex/subhook.git/' not found
#   Failed to recurse into submodule path 'edk2'
#
# Nothing in a firmware build touches UnitTestFrameworkPkg, so it is excluded
# rather than allowed to take the whole checkout down with it. Every other
# submodule does resolve and is fetched.
set -euo pipefail

echo "=== top level submodules ==="
git submodule update --init --depth 1 \
    edk2 edk2-platforms Dxe/EFI_Binaries F11/Library/SimpleInit

echo
echo "=== edk2 nested submodules, excluding the unit test framework ==="
git -C edk2 submodule init
mapfile -t paths < <(
    git -C edk2 config --file .gitmodules --get-regexp 'submodule\..*\.path$' \
        | awk '{print $2}' \
        | grep -v '^UnitTestFrameworkPkg/' \
        || true
)
printf '  %s\n' "${paths[@]}"
git -C edk2 submodule update --jobs 4 "${paths[@]}"

echo
echo "=== edk2-platforms nested submodules ==="
git -C edk2-platforms submodule init
mapfile -t ppaths < <(
    git -C edk2-platforms config --file .gitmodules --get-regexp 'submodule\..*\.path$' \
        | awk '{print $2}' || true
)
if [ ${#ppaths[@]} -gt 0 ]; then
    printf '  %s\n' "${ppaths[@]}"
    git -C edk2-platforms submodule update --jobs 4 "${ppaths[@]}"
else
    echo "  (none)"
fi

echo
echo "=== the pieces the build links against ==="
# Only what is definitely required, so a missing one stops the run early instead
# of failing 20 minutes into an edk2 build.
missing=0
for path in \
    edk2/BaseTools/Source/C/BrotliCompress/brotli \
    edk2/CryptoPkg/Library/OpensslLib/openssl \
    Dxe/EFI_Binaries \
    F11/Library/SimpleInit/SimpleInit.inc
do
    if [ -e "$path" ]; then
        echo "  ok      $path"
    else
        echo "  MISSING $path"
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    echo
    echo "a required submodule is missing; the build would fail later anyway"
    exit 1
fi

echo
echo "all required submodules are in place"