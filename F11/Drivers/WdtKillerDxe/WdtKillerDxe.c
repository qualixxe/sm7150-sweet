/** @file
 *
 *  WdtKillerDxe - mantem o APSS watchdog (0x17C10000) desabilitado de forma
 *  CONTINUA durante o DXE, via timer periodico.
 *
 *  O disable one-shot NAO basta: o proprio comentario antigo provou que o
 *  reset do boot e causado por um DRIVER posterior que RE-ARMA o watchdog.
 *  Esse driver agora e o RpmhDxe (reativado pra ligar o clock do QUPv3/I2C).
 *  Solucao: um timer periodico reescreve WDT_EN=0 a cada 50ms, matando
 *  qualquer re-arme antes do "bite". Tambem faz um disable final no
 *  ExitBootServices (handoff pro SO).
 *
 *  Layout KPSS: WDT_RST=+0x04, WDT_EN=+0x08, WDT_BARK=+0x10, WDT_BITE=+0x14
 **/

#include <PiDxe.h>

#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>

#include <Protocol/Timer.h>

#define APSS_WDT_BASE   0x17C10000
#define WDT_RST         (APSS_WDT_BASE + 0x04)
#define WDT_EN          (APSS_WDT_BASE + 0x08)
#define WDT_BARK        (APSS_WDT_BASE + 0x10)
#define WDT_BITE        (APSS_WDT_BASE + 0x14)

//
// Periodo do disabler em unidades de 100ns. 50ms = 50.000.000ns / 100 = 500000.
// O "bite" do WDT e da ordem de segundos, entao 50ms mata o re-arme com folga.
//
#define WDT_POLL_100NS  500000

STATIC EFI_EVENT  mWdtTimerEvent = NULL;
STATIC EFI_EVENT  mExitBsEvent   = NULL;

STATIC
VOID
DisableWdt (
  VOID
  )
{
  MmioWrite32 (WDT_EN,   0x00000000);
  MmioWrite32 (WDT_BITE, 0x00000000);
  MmioWrite32 (WDT_BARK, 0x00000000);
  MmioWrite32 (WDT_RST,  0x00000001);
  MmioWrite32 (WDT_EN,   0x00000000);
}

STATIC
VOID
EFIAPI
WdtTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  DisableWdt ();
}

STATIC
VOID
StartPeriodic (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mWdtTimerEvent != NULL) {
    return;
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  WdtTick,
                  NULL,
                  &mWdtTimerEvent
                  );
  if (!EFI_ERROR (Status)) {
    gBS->SetTimer (mWdtTimerEvent, TimerPeriodic, WDT_POLL_100NS);
    DEBUG ((DEBUG_ERROR, "WdtKillerDxe: periodic WDT disabler armed (50ms)\n"));
  }
}

STATIC
VOID
EFIAPI
OnTimerArch (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  VOID  *Iface;

  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiTimerArchProtocolGuid, NULL, &Iface))) {
    StartPeriodic ();
  }
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  DisableWdt ();
}

EFI_STATUS
EFIAPI
WdtKillerEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  VOID        *Iface;
  EFI_EVENT   NotifyEvent;
  VOID        *Registration;

  //
  // Disable imediato (one-shot) ao entrar - mata o WDT o quanto antes.
  //
  DisableWdt ();
  DEBUG ((DEBUG_ERROR, "WdtKillerDxe: APSS WDT disable, EN=0x%08x\n", MmioRead32 (WDT_EN)));

  //
  // Liga o disabler PERIODICO. Se o Timer arch protocol ja existe, comeca
  // agora; senao registra um notify pra comecar assim que o TimerDxe subir.
  //
  Status = gBS->LocateProtocol (&gEfiTimerArchProtocolGuid, NULL, &Iface);
  if (!EFI_ERROR (Status)) {
    StartPeriodic ();
  } else {
    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    OnTimerArch,
                    NULL,
                    &NotifyEvent
                    );
    if (!EFI_ERROR (Status)) {
      gBS->RegisterProtocolNotify (&gEfiTimerArchProtocolGuid, NotifyEvent, &Registration);
    }
  }

  //
  // Disable final no handoff pro SO (ExitBootServices).
  //
  gBS->CreateEvent (
         EVT_SIGNAL_EXIT_BOOT_SERVICES,
         TPL_NOTIFY,
         OnExitBootServices,
         NULL,
         &mExitBsEvent
         );

  return EFI_SUCCESS;
}
