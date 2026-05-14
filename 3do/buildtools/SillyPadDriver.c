/*
 * SillyPadDriver -- custom pod driverlet for the 3DO "Silly Control
 * Pad" / Arcade controller (PBUS device ID 0xC0).
 *
 * Portfolio doesn't ship a stock driver for this device class -- the
 * only retail hardware that uses it is the Orbatak arcade cabinet,
 * which baked its driver into ROM rather than relying on a shipped
 * cport.rom file. We synthesize one here so the joypad-tester ROM
 * can detect and decode the Silly Control Pad over PBUS.
 *
 * Wire format per 3dodev.com/documentation/hardware/opera/pbus
 * (and joypad-os/docs/protocols/3DO_PBUS.md):
 *
 *   Byte 0: 0xC0                              (Arcade ID)
 *   Byte 1: [P1Coin][P1Start][_][P2Coin][_][P2Start][_][Service]
 *           bit 7   bit 6      5  bit 4  3  bit 2     1  bit 0
 *
 * The driverlet decodes byte 1 into eight bit positions and surfaces
 * them through the standard ControlButton* event channel so our app
 * can consume them via apply_event_record's existing pad path. The
 * specific bit positions we set in cped_ButtonBits are documented at
 * the top of the case statement so the host renderer can decode them
 * with the matching mask values.
 *
 * Compiled with -DDriverletEntry=_Sillypad_DriverletEntry so the
 * entry symbol doesn't clash when linked into the broker alongside
 * Stick/Mouse/LightGun/Glasses driverlets.
 *
 * Licensed MIT - Copyright (c) 2026 Robert Dale Smith. See ../LICENSE.md.
 */

#ifdef CONTROLPORT

#include "types.h"
#include "debug.h"
#include "poddriver.h"

/* Match the field-layout helpers used in the official driverlets. */
#define DBUG(x)
#define DBUG2(x)

/* Bit positions we expose in the event's cped_ButtonBits word. We
 * pick the top 8 bits so the renderer can use the same byte-aligned
 * mask checks the pad uses (ControlA = 0x80000000 etc). The host
 * renderer looks for these specific masks in draw_arcade_row. */
#define ARCADE_P1_COIN   0x80000000u
#define ARCADE_P1_START  0x40000000u
#define ARCADE_P2_COIN   0x20000000u
#define ARCADE_P2_START  0x10000000u
#define ARCADE_SERVICE   0x08000000u

#ifndef EVENTBIT0_ControlButtonPressed
# include "event.h"
#endif

/* Two different bit layouts exist in the wild for the silly_pad
 * button byte, so we accept either:
 *
 *   PBUS public spec (3dodev.com):
 *     bit 7 = P1_COIN, bit 6 = P1_START,
 *     bit 4 = P2_COIN, bit 2 = P2_START, bit 0 = SERVICE
 *
 *   SNES23DO firmware (ref/SNES23DO/code/SNES23DO/main.asm):
 *     bit 5 = P1_COIN (Right Click on mouse 1, "Left Coin")
 *     bit 1 = P1_START (Left Click on mouse 1, "Left Start")
 *     bit 4 = P2_COIN (Right Click on mouse 2 / ext, "Right Coin")
 *     bit 2 = P2_START (Left Click on mouse 2 / ext, "Right Start")
 *     bit 0 = SERVICE (Middle Click / SNES-Y, "Service button")
 *
 * Both maps agree on P2_Start/P2_Coin/Service. They disagree only
 * on which bit carries P1 -- so we OR both candidate positions.
 * Bits 3 and (in the SNES23DO map) bits 6/7 are unused and stay 0,
 * so the combined mask doesn't mis-fire when only one map is live.
 */
static uint32 decode_arcade_byte (uint8 b1)
{
  uint32 out = 0;
  if (b1 & (0x80 | 0x20)) out |= ARCADE_P1_COIN;   /* spec bit7 or SNES23DO bit5 */
  if (b1 & (0x40 | 0x02)) out |= ARCADE_P1_START;  /* spec bit6 or SNES23DO bit1 */
  if (b1 & 0x10)          out |= ARCADE_P2_COIN;
  if (b1 & 0x04)          out |= ARCADE_P2_START;
  if (b1 & 0x01)          out |= ARCADE_SERVICE;
  return out;
}

static void StampFrame (EventFrame *f, Pod *p)
{
  f->ef_PodNumber       = p->pod_Number;
  f->ef_PodPosition     = p->pod_Position;
  f->ef_GenericPosition = p->pod_GenericNumber[0];  /* class-relative ordinal */
}

Err DriverletEntry (PodInterface *interfaceStruct,
                    struct KernelBase *kbase)
{
  Pod    *pod = interfaceStruct->pi_Pod;
  uint8  *base;
  uint32  decodedBits, oldBits;
  uint32  buttonsDown, buttonsUp, events;
  EventFrame *thisFrame;
  ControlPadEventData *cped;

  (void)kbase;  /* not used; the broker only consults its own KernelBase */

  switch (interfaceStruct->pi_Command)
    {
    case PD_InitDriver:
      break;

    case PD_InitPod:
      pod->pod_PrivateData[0] = 0;
      pod->pod_PrivateData[1] = 0;
      /* No POD_Is* class for SillyPad in the public flag set -- the
       * host detects via the raw PBUS byte 0xC0 in pod_Type lower
       * byte. We do set POD_ShortFramesOK so the broker can pack
       * other devices alongside us if needed. */
      pod->pod_Flags = POD_ShortFramesOK;
      break;

    case PD_ParsePodInput:
      oldBits = pod->pod_PrivateData[0];
      base = interfaceStruct->pi_ControlPortBuffers->
               mb_Segment[MB_INPUT_SEGMENT].bs_SegmentBase
             + pod->pod_InputByteOffset;
      /* On-wire layout for the 0xC0 SillyPad frame is actually 3 (or
       * 4) bytes, not 2 like the PBUS public spec claims. Verified
       * against:
       *
       *   - portfolio_os/src/input/ControlPadDriver.c case 0xC0
       *     (reads base[0..2] = 3 bytes for the 0xC0 device variant)
       *   - SNES23DO ATmega firmware
       *     ref/SNES23DO/code/SNES23DO/main.asm::TDO_Send_Silly_Pad
       *     (emits 0xC0 0x00 <buttons> 0x00 -- 4 bytes per frame,
       *     with the button byte at offset 2)
       *
       * So:
       *   base[0] = 0xC0 (device-class ID)
       *   base[1] = 0x00 (zero-pad / reserved)
       *   base[2] = button bitmask (per PBUS spec mapping)
       *   base[3] = 0x00 (zero-pad / reserved, if 4-byte variant)
       */
      decodedBits = decode_arcade_byte (base[2]);
      pod->pod_PrivateData[1] = oldBits;
      pod->pod_PrivateData[0] = decodedBits;

      buttonsDown = decodedBits & ~oldBits;
      buttonsUp   = oldBits     & ~decodedBits;
      events = EVENTBIT0_ControlButtonArrived;
      if (buttonsDown)
        events |= EVENTBIT0_ControlButtonPressed
                | EVENTBIT0_ControlButtonUpdate;
      if (buttonsUp)
        events |= EVENTBIT0_ControlButtonReleased
                | EVENTBIT0_ControlButtonUpdate;
      pod->pod_EventsReady[0] = events;
      break;

    case PD_AppendEventFrames:
      events = (interfaceStruct->pi_TriggerMask[0]
                | interfaceStruct->pi_CaptureMask[0])
               & pod->pod_EventsReady[0];
      if (events == 0 && !interfaceStruct->pi_RecoverFromLostEvents)
        break;
      if (events & EVENTBIT0_ControlButtonPressed)
        {
          thisFrame = (*interfaceStruct->pi_InitFrame)
                        (EVENTNUM_ControlButtonPressed,
                         sizeof (ControlPadEventData),
                         &interfaceStruct->pi_NextFrame,
                         &interfaceStruct->pi_EndOfFrameArea);
          if (thisFrame)
            {
              StampFrame (thisFrame, pod);
              cped = (ControlPadEventData *)&thisFrame->ef_EventData[0];
              cped->cped_ButtonBits = pod->pod_PrivateData[0]
                                      & ~pod->pod_PrivateData[1];
            }
        }
      if (events & EVENTBIT0_ControlButtonReleased)
        {
          thisFrame = (*interfaceStruct->pi_InitFrame)
                        (EVENTNUM_ControlButtonReleased,
                         sizeof (ControlPadEventData),
                         &interfaceStruct->pi_NextFrame,
                         &interfaceStruct->pi_EndOfFrameArea);
          if (thisFrame)
            {
              StampFrame (thisFrame, pod);
              cped = (ControlPadEventData *)&thisFrame->ef_EventData[0];
              cped->cped_ButtonBits = pod->pod_PrivateData[1]
                                      & ~pod->pod_PrivateData[0];
            }
        }
      if (interfaceStruct->pi_RecoverFromLostEvents
          || (events & EVENTBIT0_ControlButtonUpdate))
        {
          thisFrame = (*interfaceStruct->pi_InitFrame)
                        (EVENTNUM_ControlButtonUpdate,
                         sizeof (ControlPadEventData),
                         &interfaceStruct->pi_NextFrame,
                         &interfaceStruct->pi_EndOfFrameArea);
          if (thisFrame)
            {
              StampFrame (thisFrame, pod);
              cped = (ControlPadEventData *)&thisFrame->ef_EventData[0];
              cped->cped_ButtonBits = pod->pod_PrivateData[0];
            }
        }
      if (events & EVENTBIT0_ControlButtonArrived)
        {
          thisFrame = (*interfaceStruct->pi_InitFrame)
                        (EVENTNUM_ControlButtonArrived,
                         sizeof (ControlPadEventData),
                         &interfaceStruct->pi_NextFrame,
                         &interfaceStruct->pi_EndOfFrameArea);
          if (thisFrame)
            {
              StampFrame (thisFrame, pod);
              cped = (ControlPadEventData *)&thisFrame->ef_EventData[0];
              cped->cped_ButtonBits = pod->pod_PrivateData[0];
            }
        }
      break;

    case PD_ConstructPodOutput:
      /* SillyPad has no output bits to send; the broker still calls
       * this command on every cycle, but there's nothing to pack. */
      break;

    case PD_TeardownPod:
    case PD_ShutdownDriver:
      break;
    }

  return 0;
}

#endif /* CONTROLPORT */
