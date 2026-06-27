/** @file
 *
 *  I2cClockDxe - liga SO o clock do QUPv3 wrap1 SE1 (MMIO 0x00A84000, i2c do
 *  touch GDGT9889 = Linux i2c7) direto nos registradores do GCC, SEM o backend
 *  Qualcomm. Necessario pro qci2c7150 (moorea) falar com o touch.
 *
 *  >>> O "bypass de GPIO" (reset do IC, config do PDC, pad GPIO9, vddio GPIO90)
 *      FOI REMOVIDO. Agora o driver qcgpio7150 (QCOM140D) do moorea manda no
 *      TLMM/PDC nativamente. Enquanto o UEFI configurava o PDC, ele BRIGAVA com
 *      o qcgpio -> flood de IRQ ("Spurious GIC 0x239") -> CLOCK_WATCHDOG_TIMEOUT
 *      logo no boot. Por isso ficou SO o clock aqui. <<<
 *
 *  Clock: RCG comum liga o root sozinha com a fonte CXO viva pulsando SO o
 *  UPDATE (bit0). DIV=0 => /1 => 19.2MHz. GCC base = 0x00100000.
 **/

#include <PiDxe.h>

#include <Library/UefiDriverEntryPoint.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>

#define GCC_BASE              0x00100000

#define GCC_GPLL0_ENA         (GCC_BASE + 0x52000)   // vote do GPLL0 (bit0)
#define GCC_BRANCH_ENA_VOTE   (GCC_BASE + 0x5200C)   // vote dos branches

#define CBCR_QUP1_CORE_2X     (GCC_BASE + 0x18004)   // vote bit 18
#define CBCR_QUP1_CORE        (GCC_BASE + 0x18008)   // vote bit 19
#define CBCR_QUP1_M_AHB       (GCC_BASE + 0x1800C)   // vote bit 20
#define CBCR_QUP1_S_AHB       (GCC_BASE + 0x18010)   // vote bit 21
#define CBCR_QUP1_S1          (GCC_BASE + 0x18144)   // vote bit 23 (SE do 0xA84000)

#define RCG_QUP1_S1_CMD       (GCC_BASE + 0x18148)
#define RCG_CFG_OFF           0x4
#define RCG_M_OFF             0x8
#define RCG_N_OFF             0xC
#define RCG_D_OFF             0x10

#define VOTE_CORE_2X          (1u << 18)
#define VOTE_CORE             (1u << 19)
#define VOTE_M_AHB            (1u << 20)
#define VOTE_S_AHB            (1u << 21)
#define VOTE_S1               (1u << 23)

#define RCG_UPDATE_BIT        (1u << 0)
#define RCG_ROOT_OFF_BIT      (1u << 31)

#define CBCR_STATUS_MASK      0xF0000000u
#define CBCR_FSM_ON           (1u << 29)

//
// EFIPmicVreg (do edk2-msm QcomPkg/Include/Protocol/EFIPmicVreg.h) declarado INLINE,
// pois o edk2-sweet nao tem o QcomPkg. So o que preciso pra ligar a LDO11 (AVDD touch).
// GUID 22d38d3d-e8b6-4f8f-9c26-bceb07d6cb68. EFI_PM_LDO_11 = 10 (LDO_1=0).
//
#define PM_LDO_11   10u

typedef struct {
  UINT32   PullDown;      // EFI_PM_ON_OFF_TYPE
  UINT32   SwMode;        // EFI_PM_PWR_SW_MODE_TYPE
  UINT32   PinCtrled;     // EFI_PM_ON_OFF_TYPE
  UINT32   SwEnable;      // EFI_PM_ON_OFF_TYPE (1 = ligado)
  BOOLEAN  VregOk;
} PM_VREG_STATUS;

typedef struct _PMIC_VREG_PROTO PMIC_VREG_PROTO;
struct _PMIC_VREG_PROTO {
  UINT64      Revision;
  EFI_STATUS (EFIAPI *VregControl)        (UINT32, UINT32, BOOLEAN);
  EFI_STATUS (EFIAPI *VregSetLevel)       (UINT32, UINT32, UINT32);
  EFI_STATUS (EFIAPI *VregGetLevel)       (UINT32, UINT32, UINT32 *);
  EFI_STATUS (EFIAPI *VregSetPwrMode)     (UINT32, UINT32, UINT32);
  EFI_STATUS (EFIAPI *VregMultiphaseCtrl) (UINT32, UINT32, UINT32);
  EFI_STATUS (EFIAPI *VregGetStatus)      (UINT32, UINT32, PM_VREG_STATUS *);
  EFI_STATUS (EFIAPI *VregSetLevelUv)     (UINT32, UINT32, UINT32);
};

STATIC EFI_GUID  mPmicVregGuid = {
  0x22d38d3d, 0xe8b6, 0x4f8f, { 0x9c, 0x26, 0xbc, 0xeb, 0x07, 0xd6, 0xcb, 0x68 }
};

//
// EFI_CPU_ARCH_PROTOCOL (inline) - so pra SetMemoryAttributes (8o membro), pra MAPEAR
// as regioes chnls/obsrvr do SPMI que nao vem mapeadas na nossa UEFI stripped.
//
typedef struct _CPU_ARCH CPU_ARCH;
struct _CPU_ARCH {
  VOID        *Fn[7];                                                    // 7 membros antes
  EFI_STATUS (EFIAPI *SetMemoryAttributes) (CPU_ARCH *, UINT64, UINT64, UINT64);
  UINT32       NumberOfTimers;
  UINT32       DmaBufferAlignment;
};
STATIC EFI_GUID  mCpuArchGuid = {
  0x26baccb1, 0x6f42, 0x11d4, { 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
};

//
// --- SPMI cru (PMIC arbiter v5) ---
// Mapa APID<->PPID em core 0x0C440000 + 0x800 + 4*apid ; PPID=(reg>>8)&0xFFF=(SID<<8)|per.
// Comando por APID em chnls 0x0C600000 + apid*0x1000 (v2: 32MB/0x1000=8192 canais; obsrvr 1MB/0x80=8192 confirma):
// CMD=+0, STATUS=+8, WDATA0=+0x10, RDATA0=+0x18.
// STATUS: bit0=DONE, bit1=FAILURE, bit2=DENIED(nao somos donos do canal), bit3=DROPPED.
// opc EXT_WRITEL=0, EXT_READL=1 (endereco 16-bit). bc=len-1.
//
#define SPMI_CORE   0x0C440000u
#define SPMI_CHNLS  0x0C600000u
#define SPMI_OBSRVR 0x0E600000u   // observer (read-only, qualquer EE) - DTB reg-names; stride 0x80

STATIC
UINT32
SpmiApid (
  IN UINT32  Sid,
  IN UINT32  Per
  )
{
  UINT32  apid, want;
  want = ((Sid & 0xFu) << 8) | (Per & 0xFFu);
  for (apid = 0; apid < 512u; apid++) {   // era 256 - charger do SID 0 mora em apid > 256
    if (((MmioRead32 (SPMI_CORE + 0x800u + 4u * apid) >> 8) & 0xFFFu) == want) {
      return apid;
    }
  }
  return 0xFFFFFFFFu;
}

// Le 1 byte do PMIC. Retorna 0x00-0xFF ok; >=0x10000 = erro (com status nos bits baixos).
STATIC
UINT32
SpmiRead (
  IN UINT32  Sid,
  IN UINT32  Addr
  )
{
  UINT32  apid, st, i;
  UINTN   ch;

  apid = SpmiApid (Sid, (Addr >> 8) & 0xFFu);
  if (apid == 0xFFFFFFFFu) {
    return 0x10000u;
  }
  // (nota: SpmiApid agora varre 0-512 - o charger no SID 0 mora em apid > 256)
  ch = SPMI_CHNLS + (UINTN)apid * 0x1000u;
  MmioWrite32 (ch + 0x00u, (1u << 27) | ((Sid & 0xFu) << 20) | ((Addr & 0xFFFFu) << 4));
  st = 0;
  for (i = 0; i < 200000; i++) {
    st = MmioRead32 (ch + 0x08u);
    if (st & 0xFu) {
      break;
    }
  }
  if ((st & 0x1u) == 0) {
    return 0x20000u | (st & 0xFFu);
  }
  if (st & 0xEu) {
    return 0x30000u | (st & 0xFFu);
  }
  return MmioRead32 (ch + 0x18u) & 0xFFu;
}

// Le 1 byte do PMIC pela regiao OBSERVER (read-only, qualquer EE pode -> NAO precisa ser
// dono do canal, ao contrario do chnls). Stride 0x80. Mesmo formato de comando (EXT_READL).
STATIC
UINT32
SpmiReadObs (
  IN UINT32  Sid,
  IN UINT32  Addr
  )
{
  UINT32  apid, st, i;
  UINTN   ch;

  apid = SpmiApid (Sid, (Addr >> 8) & 0xFFu);
  if (apid == 0xFFFFFFFFu) {
    return 0x10000u;
  }
  ch = SPMI_OBSRVR + (UINTN)apid * 0x80u;
  MmioWrite32 (ch + 0x00u, (1u << 27) | ((Sid & 0xFu) << 20) | ((Addr & 0xFFFFu) << 4));
  st = 0;
  for (i = 0; i < 200000u; i++) {
    st = MmioRead32 (ch + 0x08u);
    if (st & 0xFu) {
      break;
    }
  }
  if ((st & 0x1u) == 0) {
    return 0x20000u | (st & 0xFFu);
  }
  return MmioRead32 (ch + 0x18u) & 0xFFu;
}

// Escreve 1 byte no PMIC. Retorna o STATUS (bit0=DONE ok; bit2=DENIED = nao somos donos).
STATIC
UINT32
SpmiWrite (
  IN UINT32  Sid,
  IN UINT32  Addr,
  IN UINT32  Data
  )
{
  UINT32  apid, st, i;
  UINTN   ch;

  apid = SpmiApid (Sid, (Addr >> 8) & 0xFFu);
  if (apid == 0xFFFFFFFFu) {
    return 0xFF01u;
  }
  ch = SPMI_CHNLS + (UINTN)apid * 0x1000u;
  MmioWrite32 (ch + 0x10u, Data & 0xFFu);                                            // WDATA0
  MmioWrite32 (ch + 0x00u, (0u << 27) | ((Sid & 0xFu) << 20) | ((Addr & 0xFFFFu) << 4));  // EXT_WRITEL bc=0
  st = 0;
  for (i = 0; i < 200000; i++) {
    st = MmioRead32 (ch + 0x08u);
    if (st & 0xFu) {
      break;
    }
  }
  return st;
}

STATIC
BOOLEAN
BranchIsOn (
  IN UINTN  CbcrAddr
  )
{
  UINT32  S;
  S = MmioRead32 (CbcrAddr) & CBCR_STATUS_MASK;
  return (BOOLEAN)((S == 0) || (S == CBCR_FSM_ON));
}

STATIC
VOID
PollBranchOn (
  IN UINTN   CbcrAddr,
  IN CHAR8   *Name
  )
{
  UINT32  i;
  for (i = 0; i < 2000000; i++) {
    if (BranchIsOn (CbcrAddr)) {
      return;
    }
  }
  DEBUG ((DEBUG_ERROR, "I2cClockDxe: branch %a NAO ligou\n", Name));
}

//
// >>> CAMINHO B (jun/2026): VOTO RPMh CRU pra ligar a AVDD do touch. <<<
//   O boot.img de fabrica PROVOU que a AVDD (pm6150l_l11 / recurso "ldoc11" no PM6150L)
//   e um RPMh-regulator -> o APPS NAO liga por SPMI (denied/owner=EE4), so via RPMh. O
//   RpmhDxe do dopaemon faria isso, mas o DEPEX dele puxa o DALSys que CRASHA. Entao
//   reimplemento o voto na mao, igual o kernel (cmd-db.c + rpmh-rsc.c + qcom-rpmh-regulator.c):
//     1. cmd-db (base no ptr IMEM 0xC3F000C) -> acha a entry "ldoc11" -> endereco VRM
//     2. apps_rsc (DRV @0x18220000, TCS @0x18220D00, TCS-ativo 0): escreve voltage(mV) +
//        enable(1) nos 2 slots e dispara a TCS via AMC.
//   So-leitura ate o trigger; os prints mostram cada passo (base, magic, VRM addr, versao).
//
#define IMEM_CMDDB_PTR   0xC3F000Cu
#define RSC_DRV_BASE     0x18220000u
#define RSC_TCS_BASE     0x18220D00u

STATIC
VOID
MapUc (
  IN EFI_SYSTEM_TABLE  *St,
  IN UINT64            Base,
  IN UINT64            Size
  )
{
  CPU_ARCH  *Cpu = NULL;
  if (!EFI_ERROR (St->BootServices->LocateProtocol (&mCpuArchGuid, NULL, (VOID **)&Cpu)) && (Cpu != NULL)) {
    Cpu->SetMemoryAttributes (Cpu, Base, Size, 0x1u);   // EFI_MEMORY_UC
  }
}

STATIC
VOID
RpmhVoteLdo11C (
  IN EFI_SYSTEM_TABLE  *St
  )
{
  UINT32   CmdDbBase, RscId, Ver, CmdStride;
  UINT32   RegCtl, RegEn, RegMsgid, RegAddr, RegData, RegStat;
  UINT32   VrmAddr = 0;
  UINT8    *Db;
  UINT32   i, j;
  BOOLEAN  Found = FALSE;

  DEBUG ((DEBUG_ERROR, "\n===== RPMh VOTE LDO11_C (caminho B / UEFI puro) =====\n"));

  // mapeia o IMEM (ptr) e o RSC (MMIO) - idempotente
  MapUc (St, (UINT64)(IMEM_CMDDB_PTR & ~0xFFFu), 0x1000);
  MapUc (St, RSC_DRV_BASE, 0x4000);

  // 1) base fisica da cmd-db (ptr 32-bit no IMEM) + valida magic {db 30 03 0c}
  CmdDbBase = MmioRead32 (IMEM_CMDDB_PTR);
  DEBUG ((DEBUG_ERROR, "cmd-db ptr@%08x -> base=%08x\n", IMEM_CMDDB_PTR, CmdDbBase));
  if (CmdDbBase == 0) {
    DEBUG ((DEBUG_ERROR, "  base NULA -> aborta voto\n"));
    return;
  }
  MapUc (St, (UINT64)(CmdDbBase & ~0xFFFu), 0x20000);
  Db = (UINT8 *)(UINTN)CmdDbBase;
  DEBUG ((DEBUG_ERROR, "  magic=%02x %02x %02x %02x (esp. db 30 03 0c)\n", Db[4], Db[5], Db[6], Db[7]));
  if (!((Db[4] == 0xdb) && (Db[5] == 0x30) && (Db[6] == 0x03) && (Db[7] == 0x0c))) {
    DEBUG ((DEBUG_ERROR, "  magic NAO confere -> aborta\n"));
    return;
  }

  // 2) acha a entry "ldoc11" (id de 8 bytes) -> VrmAddr (campo addr @ offset 16 da entry de 24B)
  //    header[i] @ Db+8+i*16 (slv_id@0, header_offset@2, cnt@6); entries @ Db+144+header_offset.
  for (i = 0; (i < 8u) && !Found; i++) {
    UINT8   *H   = Db + 8u + i * 16u;
    UINT16  Slv  = (UINT16)(H[0] | (H[1] << 8));
    UINT16  Hoff = (UINT16)(H[2] | (H[3] << 8));
    UINT16  Cnt  = (UINT16)(H[6] | (H[7] << 8));
    UINT8   *Ent = Db + 144u + Hoff;
    if (Slv == 0) {
      break;
    }
    for (j = 0; j < Cnt; j++) {
      UINT8  *E = Ent + j * 24u;
      if ((E[0] == 'l') && (E[1] == 'd') && (E[2] == 'o') && (E[3] == 'c') &&
          (E[4] == '1') && (E[5] == '1') && (E[6] == 0)) {
        VrmAddr = E[16] | (E[17] << 8) | (E[18] << 16) | (E[19] << 24);
        Found   = TRUE;
        break;
      }
    }
  }
  if (!Found) {
    DEBUG ((DEBUG_ERROR, "  recurso 'ldoc11' NAO achado na cmd-db -> aborta\n"));
    return;
  }
  DEBUG ((DEBUG_ERROR, "  ldoc11 VRM addr = %08x\n", VrmAddr));

  // 3) versao do RSC (rsc_id bits[23:16]) -> offsets dos registradores (v2.7 vs v3.0)
  RscId = MmioRead32 (RSC_DRV_BASE);
  Ver   = (RscId >> 16) & 0xFFu;
  if (Ver >= 3u) {
    CmdStride = 24u; RegCtl = 0x24u; RegEn = 0x2Cu; RegMsgid = 0x34u; RegAddr = 0x38u; RegData = 0x3Cu; RegStat = 0x40u;
  } else {
    CmdStride = 20u; RegCtl = 0x14u; RegEn = 0x1Cu; RegMsgid = 0x30u; RegAddr = 0x34u; RegData = 0x38u; RegStat = 0x3Cu;
  }
  DEBUG ((DEBUG_ERROR, "  RSC id=%08x ver.major=%d -> offsets v%a\n", RscId, Ver, (Ver >= 3u) ? "3.0" : "2.7"));

  // 4) escreve os 2 comandos na TCS 0 (slot0=voltage, slot1=enable). tcs_id=0 => sem o termo 672.
  //    MSGID = LEN(8)|WRITE(BIT16) = 0x10008. voltage em mV (3.3V=3300), enable=1.
  MmioWrite32 (RSC_TCS_BASE + RegMsgid + CmdStride * 0u, 0x10008u);
  MmioWrite32 (RSC_TCS_BASE + RegAddr  + CmdStride * 0u, VrmAddr + 0x0u);   // VRM_VOLTAGE
  MmioWrite32 (RSC_TCS_BASE + RegData  + CmdStride * 0u, 3300u);            // 3.3V em mV
  MmioWrite32 (RSC_TCS_BASE + RegMsgid + CmdStride * 1u, 0x10008u);
  MmioWrite32 (RSC_TCS_BASE + RegAddr  + CmdStride * 1u, VrmAddr + 0x4u);   // VRM_ENABLE
  MmioWrite32 (RSC_TCS_BASE + RegData  + CmdStride * 1u, 1u);               // enable
  MmioWrite32 (RSC_TCS_BASE + RegEn, 0x3u);                                 // CMD_ENABLE = slots 0,1

  // 5) dispara via AMC (igual __tcs_set_trigger): clr TRIGGER, clr ENABLE, set ENABLE, set ENABLE|TRIGGER.
  MmioWrite32 (RSC_TCS_BASE + RegCtl, MmioRead32 (RSC_TCS_BASE + RegCtl) & ~(1u << 24));
  MmioWrite32 (RSC_TCS_BASE + RegCtl, MmioRead32 (RSC_TCS_BASE + RegCtl) & ~(1u << 16));
  MmioWrite32 (RSC_TCS_BASE + RegCtl, (1u << 16));
  MmioWrite32 (RSC_TCS_BASE + RegCtl, (1u << 16) | (1u << 24));

  // 6) espera o AOP COMPLETAR o voto (poll COMPL=bit16 nos 2 slots) + status final.
  //    ISSUED(bit8) = AOP recebeu; COMPL(bit16) = AOP aplicou de fato (AVDD votada).
  {
    UINT32  s0 = 0, s1 = 0, k;
    for (k = 0; k < 300000u; k++) {
      s0 = MmioRead32 (RSC_TCS_BASE + RegStat + CmdStride * 0u);
      s1 = MmioRead32 (RSC_TCS_BASE + RegStat + CmdStride * 1u);
      if ((s0 & (1u << 16)) && (s1 & (1u << 16))) {
        break;
      }
    }
    St->BootServices->Stall (10000);
    DEBUG ((DEBUG_ERROR, "  CTL=%08x st0=%08x st1=%08x  -> %a\n",
            MmioRead32 (RSC_TCS_BASE + RegCtl), s0, s1,
            ((s0 & (1u << 16)) && (s1 & (1u << 16))) ? "VOTO COMPLETOU (AVDD votada)!" : "ISSUED mas COMPL nao veio"));
  }
  DEBUG ((DEBUG_ERROR, "===== fim RPMh VOTE (se 0x5D responder no diag abaixo = AVDD LIGOU!) =====\n\n"));
}

//
// >>> TIRO 2 (jun/2026): vota um BCM (Bus Clock Manager) ON, pra segurar o NoC/bus do QUP
//     ligado no Windows (qci2c trava sem ele - o qcpep desliga o NoC no idle). Mesmo RSC/TCS
//     do RpmhVoteLdo11C, mas o DATA e o BCM_TCS_CMD (soc/qcom/tcs.h):
//       (commit<<30)|(valid<<29)|((vote_x & 0x3fff)<<14)|(vote_y & 0x3fff)
//     addr = o addr do BCM no cmd-db (achados no dump: QUP0=0x50080, QUP1=0x50094).
//
STATIC
VOID
RpmhVoteBcm (
  IN EFI_SYSTEM_TABLE  *St,
  IN UINT32            BcmAddr,
  IN CHAR8             *Name
  )
{
  UINT32  Ver, RegCtl, RegEn, RegMsgid, RegAddr, RegData, RegStat;
  UINT32  Data;

  // commit=1, valid=1, vote_x=vote_y=0x1000 (bandwidth non-zero = bus LIGADO). Ajustavel.
  Data = (1u << 30) | (1u << 29) | ((0x1000u & 0x3fffu) << 14) | (0x1000u & 0x3fffu);

  MapUc (St, RSC_DRV_BASE, 0x4000);
  Ver = (MmioRead32 (RSC_DRV_BASE) >> 16) & 0xFFu;
  if (Ver >= 3u) {
    RegCtl = 0x24u; RegEn = 0x2Cu; RegMsgid = 0x34u; RegAddr = 0x38u; RegData = 0x3Cu; RegStat = 0x40u;
  } else {
    RegCtl = 0x14u; RegEn = 0x1Cu; RegMsgid = 0x30u; RegAddr = 0x34u; RegData = 0x38u; RegStat = 0x3Cu;
  }

  // 1 comando na TCS 0, slot 0
  MmioWrite32 (RSC_TCS_BASE + RegMsgid, 0x10008u);
  MmioWrite32 (RSC_TCS_BASE + RegAddr,  BcmAddr);
  MmioWrite32 (RSC_TCS_BASE + RegData,  Data);
  MmioWrite32 (RSC_TCS_BASE + RegEn,    0x1u);

  // dispara AMC
  MmioWrite32 (RSC_TCS_BASE + RegCtl, MmioRead32 (RSC_TCS_BASE + RegCtl) & ~(1u << 24));
  MmioWrite32 (RSC_TCS_BASE + RegCtl, MmioRead32 (RSC_TCS_BASE + RegCtl) & ~(1u << 16));
  MmioWrite32 (RSC_TCS_BASE + RegCtl, (1u << 16));
  MmioWrite32 (RSC_TCS_BASE + RegCtl, (1u << 16) | (1u << 24));
  St->BootServices->Stall (5000);

  DEBUG ((DEBUG_ERROR, "BCM '%a' @%08x data=%08x -> CTL=%08x st0=%08x\n",
          Name, BcmAddr, Data,
          MmioRead32 (RSC_TCS_BASE + RegCtl),
          MmioRead32 (RSC_TCS_BASE + RegStat)));
}

//
// >>> TIRO OTG - PASSO 1 / RECON (jun/2026): le SO o ownership do charger SMB5 (pm6150),
//   read-only, SEM tocar o canal (chnls). O OTG/VBUS mora no bloco DCDC@0x1100 (interrupts
//   otg-fail/otg-oc no DTB de fabrica). Blocos SMB5: CHGR@1000 DCDC@1100(OTG) BATIF@1200
//   USB@1300 DC@1400 TYPEC@1500 MISC@1600. Pergunta GO/NO-GO: o APPS e DONO desses apids?
//     owner=APPS(0) -> a UEFI escreve OTG_EN direto por SpmiWrite (Build 2 liga o VBUS).
//     owner=RPMh(4) -> SPMI bloqueado (igual LDO11) -> precisaria de voto, adaptar.
//   Le os MESMOS regs seguros que a diag da AVDD ja le (core/cnfg, nunca crasham); NAO
//   toca o canal aqui pq canal de outro EE da external-abort e perderia o log.
//
STATIC
VOID
Smb5Recon (
  IN EFI_SYSTEM_TABLE  *St
  )
{
  UINT32  Sid, Per, Ap, Own, Found = 0u;
  UINT32  Apid, Map, Ppid;

  DEBUG ((DEBUG_ERROR, "\n===== CHARGER/SMB SCAN (SID 0-15, per 0x10-0x16) =====\n"));
  for (Sid = 0; Sid < 16u; Sid++) {
    for (Per = 0x10u; Per <= 0x16u; Per++) {
      Ap = SpmiApid (Sid, Per);
      if (Ap == 0xFFFFFFFFu) {
        continue;
      }
      Own = MmioRead32 (0x0C40A000u + 0x700u + 4u * Ap) & 0x7u;
      DEBUG ((DEBUG_ERROR, "  SID%2d per%02x apid=%3d owner=EE%d %a\n", Sid, Per, Ap, Own,
              (Own == 0u) ? "<<< APPS! da pra escrever OTG" : ""));
      Found++;
    }
  }
  if (Found == 0u) {
    DEBUG ((DEBUG_ERROR, "  !! NADA em per 0x10-0x16 -> charger nao visivel pro APPS por SPMI\n"));
  }
  DEBUG ((DEBUG_ERROR, "  (per: 10=CHGR 11=DCDC/OTG 12=BATIF 13=USBIN 14=DC 15=TYPEC 16=MISC)\n"));

  DEBUG ((DEBUG_ERROR, "== APID TABLE inteira (PPID = SID<<8|per; acha o charger onde estiver) ==\n"));
  for (Apid = 0; Apid < 512u; Apid++) {
    Map  = MmioRead32 (SPMI_CORE + 0x800u + 4u * Apid);
    Ppid = (Map >> 8) & 0xFFFu;
    if (Ppid == 0u) {
      continue;
    }
    DEBUG ((DEBUG_ERROR, "ap%03d=S%d.%02x ", Apid, (Ppid >> 8) & 0xFu, Ppid & 0xFFu));
    if ((Apid & 0x7u) == 0x7u) {
      DEBUG ((DEBUG_ERROR, "\n"));
    }
  }
  DEBUG ((DEBUG_ERROR, "\n===== fim CHARGER SCAN =====\n\n"));
}

//
// >>> TIRO OTG - PASSO 2 (jun/2026): LIGA O VBUS. O SID 0 (pm6150 SMB5) e owner=APPS (EE0)
//   em TODOS os blocos (confirmado no log) -> a UEFI escreve por SpmiWrite. O OTG mora no
//   DCDC (per 0x11): CMD_OTG @ 0x1140 (DCDC_BASE 0x1100 + 0x40), bit0 = OTG_EN. Faz: (1)
//   mapeia o canal do SMB5 (apids ~264-270, > 256 = fora do mapa default), (2) DUMP read-only
//   do DCDC/USBIN/TYPEC (confirma layout + estado), (3) seta OTG_EN (read-modify-write),
//   (4) le de volta. O boost de VBUS persiste no Windows (igual a AVDD persistiu).
//
#define SMB5_SID       0u
#define DCDC_CMD_OTG   0x1140u   // DCDC_BASE(0x1100) + 0x40 ; bit0 = OTG_EN
#define OTG_EN_BIT     0x01u

STATIC
VOID
Smb5DumpBlk (
  IN UINT32  Base,
  IN UINT32  From,
  IN UINT32  To,
  IN CHAR8   *Name
  )
{
  UINT32  r;
  DEBUG ((DEBUG_ERROR, "  -- %a (base %04x) --\n", Name, Base));
  for (r = From; r <= To; r += 8u) {
    DEBUG ((DEBUG_ERROR, "   %04x: %02x %02x %02x %02x %02x %02x %02x %02x\n", Base + r,
            SpmiRead (SMB5_SID, Base + r + 0u) & 0xFFu, SpmiRead (SMB5_SID, Base + r + 1u) & 0xFFu,
            SpmiRead (SMB5_SID, Base + r + 2u) & 0xFFu, SpmiRead (SMB5_SID, Base + r + 3u) & 0xFFu,
            SpmiRead (SMB5_SID, Base + r + 4u) & 0xFFu, SpmiRead (SMB5_SID, Base + r + 5u) & 0xFFu,
            SpmiRead (SMB5_SID, Base + r + 6u) & 0xFFu, SpmiRead (SMB5_SID, Base + r + 7u) & 0xFFu));
  }
}

STATIC
VOID
Smb5OtgEnable (
  IN EFI_SYSTEM_TABLE  *St
  )
{
  UINT32  ApDcdc;
  UINT32  Typ, Sub, CmdOtg, OtgCfg, OtgCur;
  EFI_STATUS  Ms;

  DEBUG ((DEBUG_ERROR, "\n===== OTG OBSERVER PROBE (SMB5 SID0, read-only via obsrvr) =====\n"));

  ApDcdc = SpmiApid (SMB5_SID, 0x11u);
  DEBUG ((DEBUG_ERROR, "  DCDC apid=%d\n", ApDcdc));
  if (ApDcdc == 0xFFFFFFFFu) {
    DEBUG ((DEBUG_ERROR, "  !! DCDC nao achado\n===== fim PROBE =====\n\n"));
    return;
  }

  // mapeia a OBSERVER inteira (1MB). Read-only / qualquer EE -> sem o muro de EE que travou
  // a ESCRITA no chnls. Se ISTO ler o SMB5, reachable provado e o unico bloqueio e a escrita.
  Ms = gDS->AddMemorySpace (EfiGcdMemoryTypeMemoryMappedIo, SPMI_OBSRVR, 0x100000u, EFI_MEMORY_UC);
  DEBUG ((DEBUG_ERROR, "  AddMemorySpace obsrvr = %r\n", Ms));
  gDS->SetMemorySpaceAttributes (SPMI_OBSRVR, 0x100000u, EFI_MEMORY_UC);
  MapUc (St, SPMI_OBSRVR, 0x100000u);

  DEBUG ((DEBUG_ERROR, "  lendo DCDC pelo observer (se travar AQUI, o SMB5 nao responde no SPMI)...\n"));
  Typ    = SpmiReadObs (SMB5_SID, 0x1104u);        // PERPH_TYPE
  Sub    = SpmiReadObs (SMB5_SID, 0x1105u);        // PERPH_SUBTYPE
  CmdOtg = SpmiReadObs (SMB5_SID, DCDC_CMD_OTG);   // CMD_OTG (bit0=OTG_EN)
  OtgCfg = SpmiReadObs (SMB5_SID, 0x1153u);        // OTG_CFG
  OtgCur = SpmiReadObs (SMB5_SID, 0x1152u);        // OTG_CURRENT_LIMIT

  DEBUG ((DEBUG_ERROR, "  TYPE=%x SUB=%x  (valor < 0x100 = byte lido OK; >= 0x10000 = erro)\n", Typ, Sub));
  DEBUG ((DEBUG_ERROR, "  CMD_OTG(1140)=%x OTG_CFG(1153)=%x OTG_CUR(1152)=%x\n", CmdOtg, OtgCfg, OtgCur));
  DEBUG ((DEBUG_ERROR, "  -> %a\n",
          (Typ < 0x100u) ? ">>> OBSERVER LE O SMB5! reachable; muro e SO a ESCRITA (EE) -> Windows <<<" :
                           "leitura nao completou (status nos bits >= 0x10000)"));
  DEBUG ((DEBUG_ERROR, "===== fim OTG OBSERVER PROBE =====\n\n"));
}

//
// LOG) Flusher chamado no ReadyToBoot (a ESP ja esta montada nesse ponto). Le o buffer
//   de log unificado (0xA1A10000, magic 'FLOG', preenchido por Frame/InMemorySerialPortLib)
//   e grava em \UefiLog.txt em TODOS os volumes FAT (a ESP/sda34 entre eles). Assim da pra
//   puxar o log COMPLETO da UEFI via adb (montando a ESP), sem depender de foto da tela.
//
VOID
EFIAPI
LogFlushToFile (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  volatile UINT32  *Hdr  = (volatile UINT32 *)0xA1A10000ull;
  UINT8            *Data = (UINT8 *)(0xA1A10000ull + 12);
  UINT32           Len;
  UINT32           k;
  UINT8            *Buf;
  EFI_HANDLE       *Handles;
  UINTN            Count;
  UINTN            Idx;
  EFI_STATUS       Status;

  if ((Hdr[0] != 0x474F4C46u) || (Hdr[1] != 0xDEADBEEFu)) {
    return;                                  // nada foi capturado
  }
  Len = Hdr[2];
  if ((Len == 0u) || (Len > (0x200000u - 12u))) {
    return;
  }

  // O buffer 0xA1A10000 (InMemorySerialPortLib) provavelmente esta mapeado como
  // Device memory -> passar ele direto pro Fat->Write faz o CopyMem LARGO do driver
  // dar ALIGNMENT FAULT (ESR EC 0x25). Copia byte-a-byte (leitura sempre alinhada)
  // p/ um buffer NORMAL alocado (8-byte aligned), e grava ESSE no arquivo.
  Buf = NULL;
  if (EFI_ERROR (gBS->AllocatePool (EfiBootServicesData, Len, (VOID **)&Buf)) || (Buf == NULL)) {
    return;
  }
  for (k = 0; k < Len; k++) {
    Buf[k] = Data[k];
  }

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimpleFileSystemProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (EFI_ERROR (Status) || (Handles == NULL)) {
    gBS->FreePool (Buf);
    return;
  }

  for (Idx = 0; Idx < Count; Idx++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
    EFI_FILE_PROTOCOL                *Root;
    EFI_FILE_PROTOCOL                *File;
    UINTN                            WriteLen;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Idx], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
      continue;
    }
    if (EFI_ERROR (Fs->OpenVolume (Fs, &Root))) {
      continue;
    }

    // Apaga log antigo (trunca) e recria limpo.
    File = NULL;
    if (!EFI_ERROR (Root->Open (Root, &File, L"UefiLog.txt", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0)) && (File != NULL)) {
      File->Delete (File);
    }
    File = NULL;
    if (!EFI_ERROR (Root->Open (Root, &File, L"UefiLog.txt", EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0)) && (File != NULL)) {
      WriteLen = Len;
      File->Write (File, &WriteLen, Buf);
      File->Close (File);
    }
    Root->Close (Root);
  }

  gBS->FreePool (Handles);
  gBS->FreePool (Buf);
}

EFI_STATUS
EFIAPI
I2cClockEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32  Cfg;
  UINT32  Vote;
  UINT32  i;

  //
  // LOG) Reseta o buffer de log unificado (0xA1A10000) no inicio do boot e registra o
  //   flusher pro ReadyToBoot -> cada boot gera um \UefiLog.txt limpo na ESP. O reset
  //   aqui (entry roda cedo, Depex TRUE) evita anexar log de boots anteriores na RAM.
  //
  {
    volatile UINT32  *LogHdr = (volatile UINT32 *)0xA1A10000ull;
    EFI_EVENT        LogEv;
    LogHdr[0] = 0x474F4C46u;
    LogHdr[1] = 0xDEADBEEFu;
    LogHdr[2] = 0;
    EfiCreateEventReadyToBootEx (TPL_CALLBACK, LogFlushToFile, NULL, &LogEv);
  }

  //
  // 0-AVDD) SPMI cru - DIAGNOSTICO DE OWNERSHIP. Acessar o CANAL da LDO (chnls) deu
  //   external abort mesmo com o mapa OK -> assinatura de que OUTRO EE (RPMh/AOP) e o
  //   DONO do canal (hardware bloqueia o APPS). So leio a tabela de ownership (core/cnfg,
  //   que SAO acessiveis - nao crasha) pra CONFIRMAR. owner EE nos bits baixos; APPS=0.
  //   Se owner != 0 -> SPMI cru e impossivel (so via RPMh). Se =0 -> era so mapeamento.
  //
  {
    UINT32  a50 = SpmiApid (0, 0x50u);
    UINT32  a5A = SpmiApid (0, 0x5Au);

    DEBUG ((DEBUG_ERROR, "\n== SPMI-OWNERSHIP (quem e dono do canal da LDO11?) ==\n"));
    DEBUG ((DEBUG_ERROR, "per50 apid=%d  per5A(LDO11) apid=%d\n", a50, a5A));
    DEBUG ((DEBUG_ERROR, "LDO11 coreMap+800=%08x\n", MmioRead32 (0x0C440000u + 0x800u + 4u * a5A)));
    DEBUG ((DEBUG_ERROR, "LDO11 core+700=%08x cnfg+700=%08x cnfg+900=%08x\n",
            MmioRead32 (0x0C440000u + 0x700u + 4u * a5A),
            MmioRead32 (0x0C40A000u + 0x700u + 4u * a5A),
            MmioRead32 (0x0C40A000u + 0x900u + 4u * a5A)));
    DEBUG ((DEBUG_ERROR, "per50 core+700=%08x cnfg+700=%08x cnfg+900=%08x\n",
            MmioRead32 (0x0C440000u + 0x700u + 4u * a50),
            MmioRead32 (0x0C40A000u + 0x700u + 4u * a50),
            MmioRead32 (0x0C40A000u + 0x900u + 4u * a50)));
    DEBUG ((DEBUG_ERROR, "(owner EE=4=RPMh; AVDD agora via DSDT/qcpep PMICVREGVOTE LDO11_C)\n"));

    // >>> ACHADO (jun/2026, da dtbo de FABRICA decompilada): a AVDD do touch = "vtouch" =
    //     L11 do PM6150L (PMIC SECUNDARIO), NAO o PM6150 (SID 0) checado acima! A saga
    //     inteira mirou o PMIC errado. Escaneia TODOS os SIDs atras do L11 (per 0x5A) pra
    //     achar o PM6150L e ver QUEM e dono: owner=APPS(0) -> a UEFI liga o AVDD direto
    //     (sem qcpep!); owner!=0 (RPMh) -> precisa do voto (serial).
    DEBUG ((DEBUG_ERROR, "\n== SCAN LDO11 (per 0x5A) em TODOS os SIDs (achar o PM6150L) ==\n"));
    {
      UINT32  sid;
      for (sid = 0; sid < 8u; sid++) {
        UINT32  ap = SpmiApid (sid, 0x5Au);
        if (ap == 0xFFFFFFFFu) {
          DEBUG ((DEBUG_ERROR, "  SID %d: (sem per 0x5A)\n", sid));
          continue;
        }
        DEBUG ((DEBUG_ERROR, "  SID %d apid=%d  core+700=%08x cnfg+700=%08x cnfg+900=%08x\n",
                sid, ap,
                MmioRead32 (0x0C440000u + 0x700u + 4u * ap),
                MmioRead32 (0x0C40A000u + 0x700u + 4u * ap),
                MmioRead32 (0x0C40A000u + 0x900u + 4u * ap)));
      }
      DEBUG ((DEBUG_ERROR, "  (acha o SID com owner=APPS=0 -> esse a UEFI consegue ligar)\n"));
    }
  }

  //
  // >>> TIRO OTG - PASSO 1: recon de ownership do charger SMB5 (read-only, GO/NO-GO) <<<
  //
  Smb5Recon (SystemTable);

  //
  // >>> TIRO OTG - PASSO 2: SID0/DCDC e APPS -> escreve OTG_EN, liga o VBUS <<<
  //
  Smb5OtgEnable (SystemTable);

  //
  // 0-AVDD) Liga a AVDD do touch (LDO11_C/"ldoc11" no PM6150L) via VOTO RPMh cru, ANTES
  //   do VDDIO/reset. A AVDD e RPMh-managed (provado no boot.img de fabrica) -> nao da
  //   pra ligar por SPMI; voto direto no apps_rsc. Se funcionar, o scan I2C do 0x5D
  //   (passo 6) passa a responder (ACK em vez de NACK).
  //
  RpmhVoteLdo11C (SystemTable);

  //
  // 0-BCM-DUMP) TIRO 1 (jun/2026): lista os recursos BCM da cmd-db (slv_id=5) pra achar o
  //   BCM do QUP/NoC. O i2c@a84000 depende do interconnect (wrapper bus-mas 0x98/slv 0x200);
  //   o qcpep desliga o NoC no Windows -> qci2c trava. Achado o addr do BCM do QUP, o TIRO 2
  //   vota ele no RPMh (BCM_TCS_CMD). Procurar nome tipo QUP*/CE*/aggre/cnoc/SNOC.
  //
  {
    UINT32  Base = MmioRead32 (IMEM_CMDDB_PTR);
    UINT8   *Db  = (UINT8 *)(UINTN)Base;
    if ((Base != 0) && (Db[4] == 0xdb) && (Db[5] == 0x30) && (Db[6] == 0x03) && (Db[7] == 0x0c)) {
      UINT32  i, j, k;
      DEBUG ((DEBUG_ERROR, "\n== cmd-db: rsc_hdrs + recursos MAIUSCULOS (=BCM, achar QUP) ==\n"));
      for (i = 0; i < 8u; i++) {
        UINT8   *H   = Db + 8u + i * 16u;
        UINT16  Slv  = (UINT16)(H[0] | (H[1] << 8));
        UINT16  Hoff = (UINT16)(H[2] | (H[3] << 8));
        UINT16  Cnt  = (UINT16)(H[6] | (H[7] << 8));
        UINT8   *Ent = Db + 144u + Hoff;
        if ((Slv == 0) || (Cnt == 0) || (Cnt > 2000u)) {
          continue;                 // pula invalido, mas NAO para o loop
        }
        DEBUG ((DEBUG_ERROR, "rsc[%d] slv_id=%d cnt=%d:\n", i, Slv, Cnt));
        for (j = 0; j < Cnt; j++) {
          UINT8   *E = Ent + j * 24u;
          if ((E[0] >= 'A') && (E[0] <= 'Z')) {   // nome MAIUSCULO = BCM (QUP0/ACV/CE0/SNOC...)
            UINT32  Addr = E[16] | (E[17] << 8) | (E[18] << 16) | (E[19] << 24);
            CHAR8   Nm[9];
            for (k = 0; k < 8u; k++) {
              Nm[k] = ((E[k] >= 0x20) && (E[k] < 0x7f)) ? (CHAR8)E[k] : '.';
            }
            Nm[8] = 0;
            DEBUG ((DEBUG_ERROR, "  '%a' @%08x\n", Nm, Addr));
          }
        }
      }
      DEBUG ((DEBUG_ERROR, "== fim cmd-db ==\n\n"));
    }
  }

  //
  // 0-BCM-VOTE) TIRO 2: segura o NoC/bus do QUP ligado pro Windows. O touch i2c@a84000 e da
  //   qupv3_1 (wrap1) = BCM "QUP1" (@0x50094); voto tb o "QUP0" (@0x50080) por garantia.
  //   Sem o NoC votado, o qcpep desliga ele no Windows -> qci2c trava num MMIO -> freeze.
  //
  RpmhVoteBcm (SystemTable, 0x00050094u, "QUP1");
  RpmhVoteBcm (SystemTable, 0x00050080u, "QUP0");
  // TIRO 3: o path de REGISTRO/dados do QUP tb passa pelo config-NoC (CN0/CN1) e system-NoC
  //   (SN0). qup-core sozinho nao destravou -> voto o NoC tb (addrs do dump).
  RpmhVoteBcm (SystemTable, 0x000500A8u, "CN0");
  RpmhVoteBcm (SystemTable, 0x00050098u, "CN1");
  RpmhVoteBcm (SystemTable, 0x00050078u, "SN0");

  //
  // 0) VDDIO 1.8V do touch: GPIO90 (TLMM, active-high) liga o regulador fixo que
  //    alimenta o I/O do IC E os PULL-UPS do barramento I2C. SEM isso SDA/SCL ficam
  //    presos -> o comando I2C trava ATIVO (confirmado na tela: "CMD travou ATIVO").
  //    Liga ANTES do reset (sequencia Goodix: VDDIO -> reset). O I2cClockDxe so fazia
  //    o reset e ESQUECIA o VDDIO. GPIO90 CTL = base + 90*0x1000 = 0x0395A000 (IO +4).
  //
  MmioAnd32 (0x0395A000u, ~(0x7u << 2));        // GPIO90 func0 (GPIO)
  MmioOr32  (0x0395A000u, (1u << 9));           // GPIO90 output-enable
  MmioOr32  (0x0395A004u, (1u << 1));           // GPIO90 = HIGH (VDDIO 1.8V on)
  SystemTable->BootServices->Stall (5000);      // 5 ms pro rail subir

  //
  // 0b) MUX + PULL dos pinos do I2C7: GPIO6 e GPIO7 = SDA/SCL, funcao "qup11" =
  //     FUNC_SEL 1 (pinctrl-sm7150.c: PINGROUP(6/7, NORTH, qup11, ...)). SEM o mux
  //     os pinos ficam GPIO -> SDA/SCL nao chegam aos pinos. E o default tinha
  //     bits[1:0]=01 = pull-DOWN, que segura as linhas em LOW (I2C open-drain precisa
  //     pull-UP) -> GENI ve "bus busy" -> trava ATIVO. Fix: FUNC=1 + pull-UP (bits[1:0]=11).
  //     CTL: FUNC_SEL=[5:2], PULL=[1:0]. GPIO6=0x03906000, GPIO7=0x03907000.
  //
  MmioAnd32 (0x03906000u, ~((0xFu << 2) | 0x3u));   // GPIO6 limpa FUNC_SEL + PULL
  MmioOr32  (0x03906000u, (1u << 2) | 0x3u);        // GPIO6 = qup11 + pull-UP
  MmioAnd32 (0x03907000u, ~((0xFu << 2) | 0x3u));   // GPIO7 limpa FUNC_SEL + PULL
  MmioOr32  (0x03907000u, (1u << 2) | 0x3u);        // GPIO7 = qup11 + pull-UP

  //
  // 0a) Reset do touch Goodix gt9896 (GPIO8 do TLMM, ACTIVE_LOW): pulso
  //     LOW(2ms) -> HIGH(100ms) pro IC bootar o firmware. SO o reset, SEM PDC/GIC
  //     (aquilo brigava com o qcgpio). E um pulso UNICO no boot, antes do qcgpio
  //     subir - nao conflita. O _CRS do touch nao tem linha de reset, vem daqui.
  //     TLMM NORTH 0x03900000; GPIO8 CTL=+0x8000 (IO em +4).
  //
  MmioAnd32 (0x03908000u, ~(0x7u << 2));        // GPIO8 func0
  MmioOr32  (0x03908000u, (1u << 9));           // GPIO8 output-enable
  MmioAnd32 (0x03908004u, ~(1u << 1));          // GPIO8 = LOW (assert reset)
  SystemTable->BootServices->Stall (2000);      // 2 ms em reset
  MmioOr32  (0x03908004u, (1u << 1));           // GPIO8 = HIGH (libera reset)
  SystemTable->BootServices->Stall (100000);    // 100 ms pro IC bootar

  //
  // 0) Vota o GPLL0 (inofensivo - so registra o vote numa PLL que ja roda).
  //
  MmioWrite32 (GCC_GPLL0_ENA, MmioRead32 (GCC_GPLL0_ENA) | 1u);

  //
  // 1) RCG da SE1 sem MND.
  //
  MmioWrite32 (RCG_QUP1_S1_CMD + RCG_M_OFF, 0x0);
  MmioWrite32 (RCG_QUP1_S1_CMD + RCG_N_OFF, 0x0);
  MmioWrite32 (RCG_QUP1_S1_CMD + RCG_D_OFF, 0x0);

  //
  //    CFG: src_sel = 0 (CXO, sempre ligada), DIV = 0 (/1 => 19.2MHz).
  //    Limpa o campo DIV INTEIRO (8 bits).
  //
  Cfg  = MmioRead32 (RCG_QUP1_S1_CMD + RCG_CFG_OFF);
  Cfg &= ~((0x7u << 8) | (0x3u << 12) | (1u << 20) | 0xFFu);
  MmioWrite32 (RCG_QUP1_S1_CMD + RCG_CFG_OFF, Cfg);

  //
  // 2) Commit: SO o UPDATE (bit0), igual clk_bcr_update. NADA de ROOT_EN.
  //
  MmioWrite32 (RCG_QUP1_S1_CMD, MmioRead32 (RCG_QUP1_S1_CMD) | RCG_UPDATE_BIT);
  for (i = 0; i < 2000000; i++) {
    if ((MmioRead32 (RCG_QUP1_S1_CMD) & RCG_UPDATE_BIT) == 0) {
      break;
    }
  }

  //
  // 3) Vota os branches numa unica escrita.
  //
  Vote  = MmioRead32 (GCC_BRANCH_ENA_VOTE);
  Vote |= VOTE_CORE_2X | VOTE_CORE | VOTE_M_AHB | VOTE_S_AHB | VOTE_S1;
  MmioWrite32 (GCC_BRANCH_ENA_VOTE, Vote);

  //
  // 4) Espera cada branch (poll relaxado).
  //
  PollBranchOn (CBCR_QUP1_M_AHB,   "M_AHB");
  PollBranchOn (CBCR_QUP1_S_AHB,   "S_AHB");
  PollBranchOn (CBCR_QUP1_CORE_2X, "CORE_2X");
  PollBranchOn (CBCR_QUP1_CORE,    "CORE");
  PollBranchOn (CBCR_QUP1_S1,      "SE1");

  //
  // 5) DIAGNOSTICO (temporario - REMOVER depois): le o status dos branches e os
  //    registradores do GENI Serial Engine 1 (base 0x00A84000) e imprime NA TELA
  //    (FrameBufferSerialPort). So precisa do AHB clock (ja votado acima).
  //    Responde DUAS perguntas sem depender do Windows:
  //      a) o clock do controlador I2C esta REALMENTE ligado? (CBCR status, HW_PARAM)
  //      b) o Serial Engine tem FIRMWARE de I2C carregado? (FW_REVISION proto=3=I2C;
  //         proto=0 => SEM firmware => qci2c trava na 1a transacao => 0x101)
  //    Pausa 12s pra fotografar a tela.
  //
  #define SE1_BASE              0x00A84000u
  #define SE_GENI_OUTPUT_CTRL   (SE1_BASE + 0x24)
  #define SE_GENI_STATUS        (SE1_BASE + 0x40)
  #define SE_GENI_SER_M_CLK_CFG (SE1_BASE + 0x48)
  #define SE_GENI_FW_REVISION   (SE1_BASE + 0x68)   // bits[15:8] = protocolo
  #define SE_HW_PARAM_0         (SE1_BASE + 0xE24)
  #define SE_HW_PARAM_1         (SE1_BASE + 0xE28)

  DEBUG ((DEBUG_ERROR, "\n=== I2C-DIAG SE1 @A84000 ===\n"));
  DEBUG ((DEBUG_ERROR, "CBCR MAHB=%08x SAHB=%08x\n",
          MmioRead32 (CBCR_QUP1_M_AHB), MmioRead32 (CBCR_QUP1_S_AHB)));
  DEBUG ((DEBUG_ERROR, "CBCR CORE=%08x C2X=%08x S1=%08x\n",
          MmioRead32 (CBCR_QUP1_CORE), MmioRead32 (CBCR_QUP1_CORE_2X),
          MmioRead32 (CBCR_QUP1_S1)));

  DEBUG ((DEBUG_ERROR, "SE1: lendo regs (se travar aqui = AHB morto)...\n"));
  {
    UINT32  Fw    = MmioRead32 (SE_GENI_FW_REVISION);
    UINT32  Proto = (Fw >> 8) & 0xFFu;
    UINT32  Hw0   = MmioRead32 (SE_HW_PARAM_0);
    UINT32  Hw1   = MmioRead32 (SE_HW_PARAM_1);
    UINT32  Stat  = MmioRead32 (SE_GENI_STATUS);
    UINT32  Mclk  = MmioRead32 (SE_GENI_SER_M_CLK_CFG);
    UINT32  Octl  = MmioRead32 (SE_GENI_OUTPUT_CTRL);

    DEBUG ((DEBUG_ERROR, "FW=%08x PROTO=%d %a\n", Fw, Proto,
            (Proto == 3) ? "I2C-OK" : (Proto == 0) ? "SEM-FIRMWARE!!" : "OUTRO"));
    DEBUG ((DEBUG_ERROR, "HW0=%08x HW1=%08x\n", Hw0, Hw1));
    DEBUG ((DEBUG_ERROR, "ST=%08x MCLK=%08x OCTL=%08x\n", Stat, Mclk, Octl));
  }

  //
  // 6) DIAGNOSTICO-2 (temporario): LE 1 byte do IC Goodix (slave 0x5D) por I2C de
  //    verdade. So quer saber se o IC RESPONDE (ACK->manda dados) ou esta MUDO
  //    (NACK/timeout). Config minima pra ~100kHz (fonte 19.2MHz: div=7,
  //    t_high=10 t_low=11 t_cycle=26). NAO mexe no packing (usa o persistente).
  //    O reset do IC (GPIO8) ja foi pulsado la em cima (passo 0a). Imprime cru.
  //
  #define SE_DMA_MODE_EN     (SE1_BASE + 0x258)
  #define SE_I2C_RX_LEN      (SE1_BASE + 0x270)
  #define SE_I2C_SCL_CNT     (SE1_BASE + 0x278)
  #define SE_RX_PACK0        (SE1_BASE + 0x288)
  #define SE_RX_PACK1        (SE1_BASE + 0x28C)
  #define SE_M_CMD0          (SE1_BASE + 0x600)
  #define SE_M_IRQ_STATUS    (SE1_BASE + 0x610)
  #define SE_M_IRQ_CLEAR     (SE1_BASE + 0x618)
  #define SE_RX_FIFO         (SE1_BASE + 0x780)
  #define SE_RX_FIFO_STATUS  (SE1_BASE + 0x804)
  #define SE_M_GP_LENGTH     (SE1_BASE + 0x910)
  #define I2C_SLAVE_7B       0x5Du
  #define I2C_OP_READ        2u

  DEBUG ((DEBUG_ERROR, "\nI2C-RD: persist DMA=%08x SCL=%08x P0=%08x P1=%08x\n",
          MmioRead32 (SE_DMA_MODE_EN), MmioRead32 (SE_I2C_SCL_CNT),
          MmioRead32 (SE_RX_PACK0), MmioRead32 (SE_RX_PACK1)));
  DEBUG ((DEBUG_ERROR, "I2C-RD: RCG_S1 CMD=%08x CFG=%08x (bit31 CMD=ROOT_OFF)\n",
          MmioRead32 (RCG_QUP1_S1_CMD),
          MmioRead32 (RCG_QUP1_S1_CMD + RCG_CFG_OFF)));
  DEBUG ((DEBUG_ERROR, "I2C-RD: GPIO90 CTL=%08x IO=%08x | GPIO8 IO=%08x (IO bit0=nivel real)\n",
          MmioRead32 (0x0395A000u), MmioRead32 (0x0395A004u), MmioRead32 (0x03908004u)));
  DEBUG ((DEBUG_ERROR, "I2C-RD: GPIO6 CTL=%08x GPIO7 CTL=%08x (FUNC[5:2] deve ser 1=qup11)\n",
          MmioRead32 (0x03906000u), MmioRead32 (0x03907000u)));

  // FIFO mode + clock ~100kHz (uma vez)
  MmioAnd32   (SE_DMA_MODE_EN, ~1u);
  MmioWrite32 (SE_GENI_SER_M_CLK_CFG, (7u << 4) | 1u);             // div7, enable
  MmioWrite32 (SE_I2C_SCL_CNT, (10u << 20) | (11u << 10) | 26u);

  //
  // SCAN do barramento I2C7: le 1 byte de cada endereco 0x08..0x77. ACK (CMD_DONE
  // sem GP-erro) = tem chip ali; NACK (GP_IRQ bits9-14) = nada. Com o bus destravado
  // (pull-up) o Goodix pode estar em 0x5D OU 0x14 (selecao por INT/GPIO9 no reset).
  // Se NADA responder em endereco nenhum -> o bus ta vivo mas o IC nao -> falta AVDD.
  //
  {
    UINT32  Addr, n, Ms, Found = 0;

    for (Addr = 0x08u; Addr <= 0x77u; Addr++) {
      MmioWrite32 (SE_M_IRQ_CLEAR, 0xFFFFFFFFu);
      MmioWrite32 (SE_I2C_RX_LEN, 1u);
      MmioWrite32 (SE_M_CMD0, (I2C_OP_READ << 27) | (Addr << 9));

      Ms = 0;
      for (n = 0; n < 400000; n++) {
        Ms = MmioRead32 (SE_M_IRQ_STATUS);
        if (Ms & ((1u << 0) | (0x3Fu << 9))) break;               // CMD_DONE | GP-err
      }

      if ((Ms & 1u) && !(Ms & (0x3Fu << 9))) {                    // CMD_DONE e SEM GP-erro = ACK
        UINT32  Rxs = MmioRead32 (SE_RX_FIFO_STATUS) & 0x01FFFFFFu;
        UINT32  Dat = Rxs ? MmioRead32 (SE_RX_FIFO) : 0xFFFFFFFFu;
        DEBUG ((DEBUG_ERROR, ">>> I2C-SCAN: ACK em 0x%02x (data=%08x) <<<\n", Addr, Dat));
        Found++;
      }
      MmioWrite32 (SE_M_IRQ_CLEAR, 0xFFFFFFFFu);
      SystemTable->BootServices->Stall (500);                     // settle entre probes
    }

    DEBUG ((DEBUG_ERROR, "=== I2C-SCAN: %d device(s) achado(s) ===\n", Found));
    DEBUG ((DEBUG_ERROR, "I2C-SCAN VEREDITO: %a\n",
            (Found != 0) ? ">>> TEM CHIP NO BUS! (ver endereco acima) <<<" :
                           ">>> bus OK mas NINGUEM responde -> falta AVDD (LDO11) <<<"));
  }

  DEBUG ((DEBUG_ERROR, "=== I2C-DIAG fim ===\n"));

  return EFI_SUCCESS;
}
