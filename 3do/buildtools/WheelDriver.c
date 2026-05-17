/*
 * WheelDriver -- speculative pod driverlet for the 3DO steering
 * wheel (PBUS device class 15, per the public BitTable's
 *   { 40, 8 },               // Steering wheel
 * entry in EventBroker.c). Home Arcade Systems shipped one
 * commercially, supported by Need for Speed and a few others; it's
 * "ludicrously rare" and we don't have one to capture wire frames
 * against. So this driverlet:
 *
 *   1. Registers for class 0x0F (the most likely PBUS ID for a
 *      class-15 device -- if real hardware turns out to use a
 *      different byte, the AddStaticDriver call in EventBroker.c
 *      can be re-pointed).
 *
 *   2. Reads the 5 PBUS input bytes (40 bits) the BitTable promises
 *      and exposes the four payload bytes raw via StickEventData:
 *
 *        stk_HorizPosition = base[1]      // suspected steering
 *        stk_VertPosition  = base[2]      // suspected throttle pedal
 *        stk_DepthPosition = base[3]      // suspected brake pedal
 *        stk_ButtonBits    = base[4]<<24  // suspected button bits
 *
 *      The byte-to-axis mapping is a guess based on what a wheel
 *      would naturally produce; without docs or hardware we can't
 *      confirm. The tester's wheel row displays the values as raw
 *      numbers so anyone who plugs in a real Home Arcade wheel can
 *      see immediately which byte changes when they turn / press
 *      pedals / hit a button, and refine this driver.
 *
 *   3. Follows the analog joystick driverlet's "Private data
 *      assignments" convention (cur/prev buttons in slots 0-1,
 *      cur/prev X/Y/Z in slots 2-7) -- documented at the head of
 *      portfolio_os/src/input/StickDriver.c. This makes the
 *      driverlet recognisable to anyone familiar with the stick
 *      driver's structure.
 *
 *   4. Emits Stick* events rather than inventing a new family; the
 *      X/Y/Z + buttons shape fits a wheel naturally.
 *
 * Compiled with -DDriverletEntry=_Wheel_DriverletEntry so the
 * entry symbol doesn't clash when linked into the broker.
 *
 * Licensed MIT - Copyright (c) 2026 Robert Dale Smith. See ../LICENSE.md.
 */

#ifdef CONTROLPORT

#include "types.h"
#include "debug.h"
#include "poddriver.h"

#define DBUG(x)
#define DBUG2(x)

#ifndef EVENTBIT0_StickUpdate
# include "event.h"
#endif

static void StampFrame (EventFrame *f, Pod *p)
{
  f->ef_PodNumber       = p->pod_Number;
  f->ef_PodPosition     = p->pod_Position;
  f->ef_GenericPosition = p->pod_GenericNumber[GENERIC_Stick];
}

static void FillFrame (EventFrame *f, Pod *p)
{
  StickEventData *stk;
  StampFrame (f, p);
  stk = (StickEventData *)&f->ef_EventData[0];
  stk->stk_ButtonBits    = p->pod_PrivateData[0];
  stk->stk_HorizPosition = p->pod_PrivateData[2];
  stk->stk_VertPosition  = p->pod_PrivateData[3];
  stk->stk_DepthPosition = p->pod_PrivateData[4];
}

Err DriverletEntry (PodInterface *interfaceStruct,
                    struct KernelBase *kbase)
{
  Pod    *pod = interfaceStruct->pi_Pod;
  uint8  *base;
  uint32  buttons, oldButtons, buttonsDown, buttonsUp;
  int32   xPos, yPos, zPos;
  uint32  events;
  EventFrame *thisFrame;

  (void)kbase;

  switch (interfaceStruct->pi_Command)
    {
    case PD_InitDriver:
      break;

    case PD_InitPod:
      pod->pod_PrivateData[0] = pod->pod_PrivateData[1] = 0;
      pod->pod_PrivateData[2] = pod->pod_PrivateData[3] = 0;
      pod->pod_PrivateData[4] = pod->pod_PrivateData[5] = 0;
      pod->pod_PrivateData[6] = pod->pod_PrivateData[7] = 0;
      pod->pod_Flags = POD_IsStick + POD_ShortFramesOK;
      break;

    case PD_ParsePodInput:
      oldButtons = pod->pod_PrivateData[1] = pod->pod_PrivateData[0];
      base = interfaceStruct->pi_ControlPortBuffers->
               mb_Segment[MB_INPUT_SEGMENT].bs_SegmentBase
             + pod->pod_InputByteOffset;
      /* PBUS frame: 5 bytes total per BitTable. base[0] is the
       * device-class ID byte. The remaining 4 bytes are payload --
       * exact layout is speculative until real hardware confirms it. */
      xPos    = (int32)(int8)base[1];   /* signed steering, centered ~0 */
      yPos    = (int32)base[2];         /* unsigned throttle 0..255 */
      zPos    = (int32)base[3];         /* unsigned brake 0..255 */
      buttons = ((uint32)base[4]) << 24;  /* top 8 button bits */

      pod->pod_PrivateData[5] = pod->pod_PrivateData[2];  /* prev X */
      pod->pod_PrivateData[6] = pod->pod_PrivateData[3];  /* prev Y */
      pod->pod_PrivateData[7] = pod->pod_PrivateData[4];  /* prev Z */
      pod->pod_PrivateData[0] = buttons;
      pod->pod_PrivateData[2] = (uint32)xPos;
      pod->pod_PrivateData[3] = (uint32)yPos;
      pod->pod_PrivateData[4] = (uint32)zPos;

      buttonsDown = buttons    & ~oldButtons;
      buttonsUp   = oldButtons & ~buttons;
      events = EVENTBIT0_StickDataArrived;
      if (xPos != (int32)pod->pod_PrivateData[5]
       || yPos != (int32)pod->pod_PrivateData[6]
       || zPos != (int32)pod->pod_PrivateData[7]) {
        events |= EVENTBIT0_StickMoved | EVENTBIT0_StickUpdate;
      }
      if (buttonsDown)
        events |= EVENTBIT0_StickButtonPressed | EVENTBIT0_StickUpdate;
      if (buttonsUp)
        events |= EVENTBIT0_StickButtonReleased | EVENTBIT0_StickUpdate;
      pod->pod_EventsReady[0] = events;
      break;

    case PD_AppendEventFrames:
      events = (interfaceStruct->pi_TriggerMask[0]
                | interfaceStruct->pi_CaptureMask[0])
               & pod->pod_EventsReady[0];
      if (events == 0 && !interfaceStruct->pi_RecoverFromLostEvents)
        break;

      if (events & EVENTBIT0_StickButtonPressed) {
        thisFrame = (*interfaceStruct->pi_InitFrame)
                      (EVENTNUM_StickButtonPressed,
                       sizeof (StickEventData),
                       &interfaceStruct->pi_NextFrame,
                       &interfaceStruct->pi_EndOfFrameArea);
        if (thisFrame) FillFrame (thisFrame, pod);
      }
      if (events & EVENTBIT0_StickButtonReleased) {
        thisFrame = (*interfaceStruct->pi_InitFrame)
                      (EVENTNUM_StickButtonReleased,
                       sizeof (StickEventData),
                       &interfaceStruct->pi_NextFrame,
                       &interfaceStruct->pi_EndOfFrameArea);
        if (thisFrame) FillFrame (thisFrame, pod);
      }
      if (events & EVENTBIT0_StickMoved) {
        thisFrame = (*interfaceStruct->pi_InitFrame)
                      (EVENTNUM_StickMoved,
                       sizeof (StickEventData),
                       &interfaceStruct->pi_NextFrame,
                       &interfaceStruct->pi_EndOfFrameArea);
        if (thisFrame) FillFrame (thisFrame, pod);
      }
      if (interfaceStruct->pi_RecoverFromLostEvents
          || (events & EVENTBIT0_StickUpdate)) {
        thisFrame = (*interfaceStruct->pi_InitFrame)
                      (EVENTNUM_StickUpdate,
                       sizeof (StickEventData),
                       &interfaceStruct->pi_NextFrame,
                       &interfaceStruct->pi_EndOfFrameArea);
        if (thisFrame) FillFrame (thisFrame, pod);
      }
      if (events & EVENTBIT0_StickDataArrived) {
        thisFrame = (*interfaceStruct->pi_InitFrame)
                      (EVENTNUM_StickDataArrived,
                       sizeof (StickEventData),
                       &interfaceStruct->pi_NextFrame,
                       &interfaceStruct->pi_EndOfFrameArea);
        if (thisFrame) FillFrame (thisFrame, pod);
      }
      break;

    case PD_ConstructPodOutput:
      /* 8 bits out per the BitTable; we don't know what they do
       * (force-feedback / LED?) -- send zero. */
      break;

    case PD_TeardownPod:
    case PD_ShutdownDriver:
      break;
    }
  return 0;
}

#endif /* CONTROLPORT */
