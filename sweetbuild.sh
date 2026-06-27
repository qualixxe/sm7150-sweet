#!/bin/bash
# based on the instructions from edk2-platform
set -e
. build_common.sh
# 编译前需清除残留
PYTHON_COMMAND=python2.7
rm -rf workspace/Build/F11/DEBUG_GCC5/FV/Ffs/7E374E25-8E01-4FEE-87F2-390C23C606CDFVMAIN
# not actually GCC5; it's GCC7 on Ubuntu 18.04.
# >>> Recompila a DSDT (DSDT.dsl -> DSDT.aml) ANTES do build, pra garantir que edits no
#     DSDT (ex: reativar o TSC1) entrem no firmware. Sem isso, o build usa o .aml VELHO.
echo "[DSDT] recompilando DSDT.dsl -> DSDT.aml ..."
( cd F11/AcpiTables && iasl DSDT.dsl )
GCC5_AARCH64_PREFIX=aarch64-linux-gnu- build -s -n 0 -a AARCH64 -t GCC5 -p F11/sweet.dsc
gzip -c < Build/F11/DEBUG_GCC5/FV/F11_UEFI.fd >uefi_image5
cat sweet.dtb >> uefi_image5
abootimg --create sweet_uefi.img -k uefi_image5 -r ramdisk-null -f bootimg.cfg
echo "================ [VERIFY] ================"
echo "[VERIFY] FVs com GDGT9889 (TSC1/touch):"; grep -al GDGT9889 Build/F11/DEBUG_GCC5/FV/*.Fv 2>/dev/null || echo "   NENHUM -> TSC1 NAO entrou no firmware!"
echo "[VERIFY] FVs com QCOM1411 (IC10):"; grep -al QCOM1411 Build/F11/DEBUG_GCC5/FV/*.Fv 2>/dev/null || echo "   NENHUM"
echo "========================================="
rm uefi_image* Build/ -rf
