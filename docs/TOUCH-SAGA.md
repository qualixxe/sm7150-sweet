# Touch no Windows (sweet / sm7150) — Estado & Plano

> Doc de referência pra não nos perdermos. Atualizar conforme avança.
> Última atualização: 2026-06-18

## 🎯 Objetivo
Fazer o **touch (Goodix GDGT9889 / gt9896)** funcionar no Windows 11 ARM64 no
Redmi Note 10 Pro (**sweet**, sm7150), usando o UEFI **edk2-sweet**.

## 📍 TL;DR do estado
- ✅ **Windows boota ESTÁVEL** (com o touch DESATIVADO via `_STA=0` no DSDT).
- ❌ **Com o touch ATIVADO → `CLOCK_WATCHDOG_TIMEOUT (0x101)`.**
- 🎯 **Causa raiz PROVADA: a AVDD do touch (PMIC LDO11_C, 3.3V) está DESLIGADA.**
- 🚧 Todos os caminhos pra ligar a AVDD esbarraram em paredes (tabela abaixo).
- ⚠️ **Estamos cegos no Windows** (sem dump, sem debug ao vivo) → mudanças às cegas.

## 🔬 Causa raiz (provada no UEFI, via DEBUG na tela)
1. UEFI: controlador I2C (GENI SE1 @0xA84000, "IC10") **100% pronto** — `FW=0x303`
   (proto=3=I2C), clocks (AHB/RCG) on, regs acessíveis.
2. Liguei **VDDIO (GPIO90)** + muxei **SDA/SCL (GPIO6/7 → qup11, func1)** + pull-up +
   **reset (GPIO8)** → **bus destravou** (NACK rápido em vez de travar ativo).
3. **Scan I2C 0x08–0x77 = 0 devices.** Bus elétrico perfeito, IC **mudo em todo endereço**.
4. → Falta **AVDD = PMIC LDO11_C (3.3V)** (`vreg_l11c`). Só ligamos VDDIO + reset;
   **nunca a AVDD**.
5. Sem AVDD: IC mudo **e** o pino INT (GPIO9, level-low) solto → IRQ de nível
   **nunca é limpa** → **storm** → **0x101**.

**Confirmação cruzada (dado do user):** UEFI antigo (sem nossas mudanças) = Windows
estável, sem touch. Nosso UEFI (touch ativado) = 0x101. → é a nossa habilitação do
touch **SEM a AVDD**.

## 🧱 O que já tentamos pra ligar a AVDD (e por que falhou)

| Caminho | O que é | Resultado |
|---|---|---|
| **SPMI cru (UEFI)** | Escrever direto no canal da LDO11_C via arbiter SPMI | ❌ **Ownership**: canal da LDO é do **EE4 (RPMh/AOP)**, não do APPS → external abort. |
| **RpmhDxe (UEFI)** | Pedir pro RPMh (EE4) ligar a LDO | ❌ DEPEX puxa o **DALSys**, que **crasha** (data abort, ESR=0x96000006, FAR~0x6FE0C9D8). Binários dopaemon não rodam em subset. |
| **qcpep `PMICVREGVOTE`** — IC10/DSTATE | Voto de regulador na tabela do controlador | ❌ 0x101 persistiu. |
| **qcpep BSRC** — TSC1 | Voto LDO11_C na BSRC do próprio touch | ❌ 0x101 persistiu. |
| **qcpep + reorder do `_PS0`** | Votar AVDD ANTES do reset | ❌ 0x101 persistiu. |
| **Driver: timeout no I2C** | Evitar o hang sem-timeout | ❌ 0x101 (não era o driver). |
| **Driver: ISR vazio (zero I2C)** | Isolar se o crash é do driver | ❌ 0x101 → **PROVA que NÃO é o driver** (é o storm de HW). |

**Fato âncora:** o `PMICVREGVOTE` **funciona** numa tabela DEVICE/DSTATE — provado no
**miatoll (sm7125)**, cujo device UFS vota seus rails assim. O mecanismo existe; o nosso
voto do touch não "pega" por motivo que **não conseguimos ver** (falta debug no Windows).

## 🚧 O gargalo: cegueira no Windows
- Sem `pagefile.sys` na MainOS → **sem dump de crash em runtime** (os `setupmem.dmp`
  antigos eram da fase de SETUP).
- Sem debug ao vivo (KDNET) configurado.
- → Toda mudança no DSDT/qcpep é **às cegas**. É por isso que falhamos sem entender o porquê.

## 🛣️ Caminhos restantes
1. **Ganhar visibilidade no Windows** (dump / KDNET ao vivo / serial UART) → o caminho
   qcpep vira depurável: dá pra ver se o qcpep TENTA votar a LDO11_C e por que falha.
2. **Consertar o crash do DALSys** → libera o RpmhDxe no UEFI → liga a AVDD antes do
   Windows. (Self-contained, depurável na tela do UEFI, mas RE profundo.)
3. (Hipótese) o backend stripped não tem o recurso RPMh da LDO11_C → o qcpep não consegue
   votar nem com o DSDT certo. Visibilidade confirma/derruba isso.

## 🧰 Procedimentos (comandos prontos)

### Build do driver (WDK 10.0.28000)
```
msbuild "E:\cel\cel\drivers\gtx9886-driver\vhidmini2.sln" /p:Configuration=Release /p:Platform=ARM64 /t:Rebuild /p:SkipPackageVerification=true /p:EnableInf2cat=false
```
Saída: `E:\cel\cel\drivers\gtx9886-driver\driver\kmdf\ARM64\Release\vhidmini.sys`.
As 2 flags são **obrigatórias** (pulam InfVerif e inf2cat; o .sys sai test-assinado mesmo assim).

### Deploy do driver (acesso direto ao bloco, sem mount)
```
adb push <vhidmini.sys> /tmp/vhidmini.sys
adb shell /tmp/ntfscp /dev/block/sda35 /tmp/vhidmini.sys /Windows/System32/drivers/vhidmini.sys
```
- adb: `D:\sweet\platform-tools\adb.exe`
- MainOS = `/dev/block/sda35`
- ntfstools em `E:\sweet_drivers\ntfstools` (push p/ /tmp + `chmod 755`)
- `ntfsfix /dev/block/sda35` limpa a hibernação (volume leve) p/ edits aplicarem no boot full.

### DSDT (iasl)
- Decompilar referência: `wsl bash -c "cd <dir> && iasl -d DSDT.aml"`
- Compilar o nosso: `wsl bash -c "cd <AcpiTables> && iasl DSDT.dsl"`

### Debug
- **UEFI:** `FrameBufferSerialPortLib` joga `DEBUG()` **na tela** → fotografar. (Foi assim que vimos o I2C.)
- **Windows (quando houver dump):** `E:\Windows Kits\10\Debuggers\x64\cdb.exe`

## 📁 Arquivos & caminhos
- UEFI: `\\wsl.localhost\ubuntu\home\cristiano\sweet\uefi\edk2-sweet`
- DSDT: `F11\AcpiTables\DSDT.dsl` (backups: `.pre-tsc`, `.pre-avdd`, `.aei-bak`)
- Diagnóstico I2C/SPMI (**TEMPORÁRIO**, remover): `F11\Drivers\I2cClockDxe\I2cClockDxe.c`
- Driver touch (fonte): `E:\cel\cel\drivers\gtx9886-driver` (vhidmini.c)
  — **tem edits de diagnóstico (timeout + ISR vazio) a reverter.**
- Driver pack ref (surya/poco): `E:\sweet_drivers\_surya_poco_REF`
- edk2-msm fresco (surya): `E:\cel\sweet\win_on_phone\edk2-msm-master`

## 🗺️ Mapa de hardware (touch)
- IC: Goodix gt9896 / GDGT9889 @ I2C **0x5D**, barramento i2c7 = GENI SE1 @ **0xA84000** (IC10).
- SDA/SCL = GPIO6/7 (func "qup11"=1). INT = GPIO9 (level-low). RESET = GPIO8 (active-low).
- **AVDD = PMIC LDO11_C (vreg_l11c), 3.3V** ← **o que falta.**
- VDDIO = GPIO90 (já ligado).
- LDO11_C dona = **EE4 (RPMh)**. Recurso ACPI: `PPP_RESOURCE_ID_LDO11_C`.

## ⏭️ Plano atual: visibilidade no Windows via KDNET ao vivo
**Decidido (2026-06-18):** parar de chutar às cegas. Conectar o WinDbg ao cel via **KDNET
(EEM, USB)** pra VER o que o qcpep faz com a AVDD.

Passos:
1. [x] **ESP = `/dev/block/sda34`** (FAT32), BCD em `\EFI\Microsoft\Boot\BCD`.
2. [x] Lido: debug estava **OFF**, `debugtype Local` (KDNET não configurado). Bom: `nointegritychecks`+`testsigning` ON.
3. [x] **Configurado (offline + devolvido pra ESP, hash MATCH):** DBG2 entry 1 = `\_SB.USB0`/NET (`busparams 1`), transporte `kd_8003_5143.dll` presente. BCD: `debug Yes`, NET, **hostip 169.254.255.255, port 50005, key 1234.5678.9abc.def0, busparams 1, dhcp No**.
4. [ ] **EM ANDAMENTO:** WinDbg (Attach to Kernel → Net → port 50005, key 1234.5678.9abc.def0) + firewall OFF + cabo USB 3.0 C→A + bootar Windows → conectar.
5. [ ] Com olhos: `!analyze -v` (confirmar storm) + ler reg SPMI da `LDO11_C` (a AVDD ligou com o voto qcpep?) → consertar.

**Procedimento de edição offline do BCD** (caso precise refazer): puxar BCD pra PC → editar com `bcdedit /store <bcd> ...` (admin; **debugtype NET primeiro, params com `:` ex `HOSTIP:x PORT:y KEY:z`**; busparams/dhcp via `/set "{dbgsettings}" ...`) → devolver pra ESP (mount rw + cp + conferir hash). adb: `D:\sweet\platform-tools\adb.exe`.
