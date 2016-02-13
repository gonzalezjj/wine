/*
 * The Wine project - XInput Joystick Library - VirtualKey helper methods
 *
 * Copyright 2016 Juan Jose Gonzalez
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * The exported methods calculate whether or not to emit a VirtualKey press for XInputGetKeystrokes
 * based on an update on the position of the controllers thumb pads or triggers
 *
 * The switch from pressed to unpreseed for any VK is performed with hysteresis, ie. there is a
 * margin outside of the current VK's "active" area where the code will not emit a button-up event.
 * This is useful to reduce ringing, ie quick changes between states caused by minor changes in the
 * reported control values.
 *
 * Thumpad considerations:
 *  - The values for the thumbpads are mapped onto the range -1 to 1 to make a unit circle
 *  - The center region, or dead-zone, is the circle of radius 0.5
 *  - Each VirtualKey is alloted a 45-degree segment on the ring of radius 0.5 to 1
 *
 * Each thumbpad has eight VirtualKeys, one for each side and one for each corner. To simplify
 * the detection of when an area is entered and when it is left, the entire value range of the
 * thumbpad is mapped onto a 45-degree area, where the 22.5-degree bisecting line marks the border
 * between a side and a corner. The mapping is as follows:
 *  - map the value range onto the first quadrant (90-degree segment) by removing the
 *      sign from x and y, thereby mirroring along the x and y axis as needed
 *  - mirror the area between 45 and 90 degrees to the area between 0 and 45 degrees by swapping x
 *      and y if y is larger than x, thereby mirroring along the 45-degree line as needed
 *
 * The dead-zone region is left when the distance to the center (0,0) is greater than (0.5 - margin)
 * A VirtualKey region is left when (ORed):
 *  - The distance to the center (0,0) is less than (0.5 - margin)
 *  - The current position is not on the VKs area and the distance from the 22.5-degree bisecting
 *      line is greater than (margin). This is calculated by projecting the current position onto a
 *      vector P of unit lenght which points towards 112.5 degrees, ie. perpendicular to the 22.5
 *      degree line. The length of this projection, ie. the distance to the 22.5 degree line, is
 *      (P.x * x + P.Y * y)
 *
 * Chosen margin: 0.07 (for a total hysteresis region of 0.14 or 14% of the radius)
 */


#include "config.h"

#include "wine/debug.h"

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "xinput.h"

#include "xinput_backend.h"
#include "xinput_core.h"

#define SQUARE(a) (a * a)

WINE_DEFAULT_DEBUG_CHANNEL(xinput);

static const int REPEAT_DELAY_MS = 500;
static const int REPEAT_PERIOD_MS = 180;

static const float HYSTERESIS_MARGIN = 0.07;
static const float DEADZONE = 0.5;

static void Swap(float *x, float *y) {
    float z;
    z = *x;
    *x = *y;
    *y = z;
}

static WORD BtnGetVirtualKey(XINPUTW_VK_CONTROLNAME control) {
    switch (control) {
        case WVK_BTN_A: return VK_PAD_A;
        case WVK_BTN_B: return VK_PAD_B;
        case WVK_BTN_Y: return VK_PAD_Y;
        case WVK_BTN_X: return VK_PAD_X;
        case WVK_BTN_START: return VK_PAD_START;
        case WVK_BTN_BACK: return VK_PAD_BACK;
        case WVK_BTN_LSHOULDER: return VK_PAD_LSHOULDER;
        case WVK_BTN_RSHOULDER: return VK_PAD_RSHOULDER;
        case WVK_BTN_LTHUMB: return VK_PAD_LTHUMB_PRESS;
        case WVK_BTN_RTHUMB: return VK_PAD_RTHUMB_PRESS;
        case WVK_BTN_DPAD_UP: return VK_PAD_DPAD_UP;
        case WVK_BTN_DPAD_DOWN: return VK_PAD_DPAD_DOWN;
        case WVK_BTN_DPAD_LEFT: return VK_PAD_DPAD_LEFT;
        case WVK_BTN_DPAD_RIGHT: return VK_PAD_DPAD_RIGHT;
        case WVK_AXIS_LTRIGGER: return VK_PAD_LTRIGGER;
        case WVK_AXIS_RTRIGGER: return VK_PAD_RTRIGGER;
        default: return 0;
    }
}

static WORD LThumbGetVirtualKey(XINPUTW_VK_AREA area) {
    switch (area) {
        case VK_AREA_L: return VK_PAD_LTHUMB_LEFT;
        case VK_AREA_LD: return VK_PAD_LTHUMB_DOWNLEFT;
        case VK_AREA_D: return VK_PAD_LTHUMB_DOWN;
        case VK_AREA_RD: return VK_PAD_LTHUMB_DOWNRIGHT;
        case VK_AREA_R: return VK_PAD_LTHUMB_RIGHT;
        case VK_AREA_RU: return VK_PAD_LTHUMB_UPRIGHT;
        case VK_AREA_U: return VK_PAD_LTHUMB_UP;
        case VK_AREA_LU: return VK_PAD_LTHUMB_UPLEFT;
        default: return 0;
    }
}

static WORD RThumbGetVirtualKey(XINPUTW_VK_AREA area) {
    switch (area) {
        case VK_AREA_L: return VK_PAD_RTHUMB_LEFT;
        case VK_AREA_LD: return VK_PAD_RTHUMB_DOWNLEFT;
        case VK_AREA_D: return VK_PAD_RTHUMB_DOWN;
        case VK_AREA_RD: return VK_PAD_RTHUMB_DOWNRIGHT;
        case VK_AREA_R: return VK_PAD_RTHUMB_RIGHT;
        case VK_AREA_RU: return VK_PAD_RTHUMB_UPRIGHT;
        case VK_AREA_U: return VK_PAD_RTHUMB_UP;
        case VK_AREA_LU: return VK_PAD_RTHUMB_UPLEFT;
        default: return 0;
    }
}

/*
 * Maps an XINPUTW_VK_AREA to the virtual key it would emit for a specific control
 */
static WORD GetVirtualKey(XINPUTW_VK_CONTROLNAME control, XINPUTW_VK_AREA area) {
    if (control >= WVK_BTN_A && control <= WVK_AXIS_RTRIGGER)
        return area != VK_AREA_NONE ? BtnGetVirtualKey(control) : 0;
    switch (control) {
        case WVK_AXIS_LTHUMB: return LThumbGetVirtualKey(area);
        case WVK_AXIS_RTHUMB: return RThumbGetVirtualKey(area);
        default: return 0;
    }

}

/*
 * Maps XInput Wine event codes to their respective controls
 */
static XINPUTW_VK_CONTROLNAME GetControlFromEventCode(XINPUTW_EVENT_CODE code) {
    switch (code) {
        case WINE_BTN_A: return WVK_BTN_A;
        case WINE_BTN_B: return WVK_BTN_B;
        case WINE_BTN_Y: return WVK_BTN_Y;
        case WINE_BTN_X: return WVK_BTN_X;
        case WINE_BTN_START: return WVK_BTN_START;
        case WINE_BTN_BACK: return WVK_BTN_BACK;
        case WINE_BTN_LSHOULDER: return WVK_BTN_LSHOULDER;
        case WINE_BTN_RSHOULDER: return WVK_BTN_RSHOULDER;
        case WINE_BTN_LTHUMB: return WVK_BTN_LTHUMB;
        case WINE_BTN_RTHUMB: return WVK_BTN_RTHUMB;
        case WINE_BTN_DPAD_UP: return WVK_BTN_DPAD_UP;
        case WINE_BTN_DPAD_DOWN: return WVK_BTN_DPAD_DOWN;
        case WINE_BTN_DPAD_LEFT: return WVK_BTN_DPAD_LEFT;
        case WINE_BTN_DPAD_RIGHT: return WVK_BTN_DPAD_RIGHT;
        case WINE_AXIS_LTRIGGER: return WVK_AXIS_LTRIGGER;
        case WINE_AXIS_RTRIGGER: return WVK_AXIS_RTRIGGER;
        case WINE_AXIS_LTHUMB_X:
        case WINE_AXIS_LTHUMB_Y:
            return WVK_AXIS_LTHUMB;
        case WINE_AXIS_RTHUMB_X:
        case WINE_AXIS_RTHUMB_Y:
            return WVK_AXIS_RTHUMB;
        default: return (XINPUTW_VK_CONTROLNAME)-1;
    }
}

/*
 * Checks whether the requested button is pressed in the XInput state
 */
static BOOL BtnIsPressedMask(WORD wButtons, XINPUTW_VK_CONTROLNAME control) {
    switch (control) {
        case WVK_BTN_A: return (wButtons & XINPUT_GAMEPAD_A) != 0;
        case WVK_BTN_B: return (wButtons & XINPUT_GAMEPAD_B) != 0;
        case WVK_BTN_Y: return (wButtons & XINPUT_GAMEPAD_Y) != 0;
        case WVK_BTN_X: return (wButtons & XINPUT_GAMEPAD_X) != 0;
        case WVK_BTN_START: return (wButtons & XINPUT_GAMEPAD_START) != 0;
        case WVK_BTN_BACK: return (wButtons & XINPUT_GAMEPAD_BACK) != 0;
        case WVK_BTN_LSHOULDER: return (wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        case WVK_BTN_RSHOULDER: return (wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        case WVK_BTN_LTHUMB: return (wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        case WVK_BTN_RTHUMB: return (wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        case WVK_BTN_DPAD_UP: return (wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        case WVK_BTN_DPAD_DOWN: return (wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        case WVK_BTN_DPAD_LEFT: return (wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        case WVK_BTN_DPAD_RIGHT: return (wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
        default: return FALSE;
    }
}

/*
 * Checks whether the requested trigger is pressed in the XInput state, applying hysteresis
 */
static BOOL TriggerIsPressed(BYTE triggerValue, XINPUTW_VK_AREA currentArea) {
    return (currentArea == VK_AREA_PRESSED && triggerValue > 255 * (DEADZONE - HYSTERESIS_MARGIN))
        || (currentArea != VK_AREA_PRESSED && triggerValue > 255 * (DEADZONE + HYSTERESIS_MARGIN));
}

/*
 * Checks if the thumbpad's location is whithin the correct 90-degree zone
 * of the currently active area
 */
static BOOL CheckValidCurrentArea(BOOL mirroredX, BOOL mirroredY, BOOL mirroredDiag, XINPUTW_VK_AREA currentArea) {
    switch (currentArea) {
        case VK_AREA_L: return !mirroredDiag && mirroredX;
        case VK_AREA_R: return !mirroredDiag && !mirroredX;
        case VK_AREA_U: return !mirroredDiag && !mirroredY;
        case VK_AREA_D: return !mirroredDiag && mirroredY;
        case VK_AREA_LU: return mirroredX && !mirroredY;
        case VK_AREA_RU: return !mirroredX && !mirroredY;
        case VK_AREA_LD: return mirroredX && mirroredY;
        case VK_AREA_RD: return !mirroredX && mirroredY;
        default:
            WARN("invalid parameter currentArea: %d", currentArea);
            return FALSE;
    }
}

/*
 * Pushes a VK event if the current button or trigger state differs from the previous
 */
static void UpdateButton(DWORD slot_index, ULONGLONG timestamp, XINPUTW_VK_CONTROLNAME control,
                         BOOL isPressed, XINPUTW_VK_STATES *vkStates,
                         XINPUTW_KEYSTROKE_QUEUE * keystrokeQueue) {
    XINPUTW_VK_STATE * vkState;
    XINPUTW_VK_AREA newArea;
    XINPUTW_KEYSTROKE keystroke;

    newArea = isPressed ? VK_AREA_PRESSED : VK_AREA_NONE;

    vkState = &(vkStates->items[control]);
    if (vkState->area != newArea) {
        memset((void*)&keystroke, 0, sizeof(XINPUTW_KEYSTROKE));
        keystroke.timestamp = timestamp;
        keystroke.keystroke.Flags = isPressed ? XINPUT_KEYSTROKE_KEYDOWN : XINPUT_KEYSTROKE_KEYUP;
        keystroke.keystroke.UserIndex = slot_index;
        keystroke.keystroke.VirtualKey = GetVirtualKey(control, VK_AREA_PRESSED);
        xiw_vk_KeystrokeQueuePush(keystrokeQueue, &keystroke);

        vkState->area = newArea;
        vkState->timestamp = timestamp;
        vkState->isRepeat = FALSE;
    }
}

/*
 * Pushes VK events if the current thumbpad area differs from the previous
 */
static void UpdateThumb(DWORD slot_index, ULONGLONG timestamp, XINPUTW_VK_CONTROLNAME control,
                        XINPUTW_VK_AREA newArea, XINPUTW_VK_STATES *vkStates,
                        XINPUTW_KEYSTROKE_QUEUE * keystrokeQueue) {
    XINPUTW_VK_STATE * vkState;
    XINPUTW_KEYSTROKE keystroke;

    vkState = &(vkStates->items[control]);
    if (vkState->area != newArea) {
        memset((void*)&keystroke, 0, sizeof(XINPUTW_KEYSTROKE));
        keystroke.timestamp = timestamp;
        keystroke.keystroke.UserIndex = slot_index;

        if (vkState->area != VK_AREA_NONE) {
            keystroke.keystroke.Flags = XINPUT_KEYSTROKE_KEYUP;
            keystroke.keystroke.VirtualKey = GetVirtualKey(control, vkState->area);
            xiw_vk_KeystrokeQueuePush(keystrokeQueue, &keystroke);
        }

        if (newArea != VK_AREA_NONE) {
            keystroke.keystroke.Flags = XINPUT_KEYSTROKE_KEYDOWN;
            keystroke.keystroke.VirtualKey = GetVirtualKey(control, newArea);
            xiw_vk_KeystrokeQueuePush(keystrokeQueue, &keystroke);
        }

        vkState->area = newArea;
        vkState->timestamp = timestamp;
        vkState->isRepeat = FALSE;
    }
}

/*
 * Calculates the new area of for a thumbpad
 */
static XINPUTW_VK_AREA GetThumbArea(SHORT x, SHORT y, BOOL * thumbIsSquare, XINPUTW_VK_AREA currentArea) {
    /* projection vector perpendicular to the 22.5 degree line */
    const float projectionX = -0.38268343236, projectionY = 0.92387953251;
    /* distance from the mapped point to the 22.5 line */
    float projectionDistance;

    /* x and y for the mapped 45-decree sector, normalized */
    float nx, ny;
    BOOL mirroredX, mirroredY, mirroredDiag;

    float r;

    mirroredX = x < 0;
    mirroredY = y < 0;
    nx = (mirroredX ? -(x + 1) : x) / 32767.0;
    ny = (mirroredY ? -(y + 1) : y) / 32767.0;
    mirroredDiag = ny > nx;
    if (mirroredDiag)
        Swap(&nx, &ny);

    r = nx*nx + ny*ny;
    if (r > 1.4) {
        TRACE("detected a square thumbpad area\n");
        *thumbIsSquare = TRUE;
    }

    if (r > 0.1 && *thumbIsSquare) {
        TRACE("mapping square are coords with radius %f", r);
        r = (SQUARE(SQUARE(nx)) + SQUARE(nx) * SQUARE(ny)) / r;
        TRACE("  -> new radius: %f", r);
    }


    if ((currentArea == VK_AREA_NONE && r < SQUARE(DEADZONE + HYSTERESIS_MARGIN))
        || (currentArea != VK_AREA_NONE && r < SQUARE(DEADZONE - HYSTERESIS_MARGIN)))
        return VK_AREA_NONE;

    projectionDistance = nx * projectionX + ny * projectionY;

    /* Check if we're in the right 90-degree zone */
    if (CheckValidCurrentArea(mirroredX, mirroredY, mirroredDiag, currentArea)) {
        /* Check if we're also in the correct 45-degree + margin segment */
        switch (currentArea) {
            case VK_AREA_L:
            case VK_AREA_R:
            case VK_AREA_U:
            case VK_AREA_D:
                if (projectionDistance <= HYSTERESIS_MARGIN) return currentArea;
            case VK_AREA_LU:
            case VK_AREA_RU:
            case VK_AREA_LD:
            case VK_AREA_RD:
                if (projectionDistance > -HYSTERESIS_MARGIN) return currentArea;
            default:
                WARN("currentArea should not be %d\n", currentArea);
        }
    }

    /* At this point we've exited the previous area and need to find the new one */
    if (mirroredX) {
        if (mirroredY) {
            if(projectionDistance > 0) return VK_AREA_LD;
        } else {
            if(projectionDistance > 0) return VK_AREA_LU;
        }
        if (!mirroredDiag) return VK_AREA_L;
    } else {
        if (mirroredY) {
            if(projectionDistance > 0) return VK_AREA_RD;
        } else {
            if(projectionDistance > 0) return VK_AREA_RU;
        }
        if (!mirroredDiag) return VK_AREA_R;
    }
    return mirroredY ? VK_AREA_D : VK_AREA_U;
}


void xiw_vk_Update(DWORD slot_index, ULONGLONG timestamp, XINPUTW_EVENT_CODE code,
                   const XINPUT_STATE * state, XINPUTW_VK_STATES * vkStates,
                   XINPUTW_KEYSTROKE_QUEUE * keystrokeQueue) {
    XINPUTW_VK_CONTROLNAME control;

    control = GetControlFromEventCode(code);
    if (control == (XINPUTW_VK_CONTROLNAME)-1) {
        WARN("unknown event code %d\n", code);
        return;
    }

    if (control >= WVK_BTN_A && control <= WVK_BTN_DPAD_RIGHT) {
        UpdateButton(slot_index, timestamp, control, BtnIsPressedMask(state->Gamepad.wButtons, control),
                     vkStates, keystrokeQueue);
    } else if (control == WVK_AXIS_LTRIGGER || control == WVK_AXIS_RTRIGGER) {
        UpdateButton(slot_index, timestamp, control,
                     TriggerIsPressed(control == WVK_AXIS_LTRIGGER
                        ? state->Gamepad.bLeftTrigger : state->Gamepad.bRightTrigger,
                        vkStates->items[control].area),
                     vkStates, keystrokeQueue);
    } else if (control == WVK_AXIS_LTHUMB || control == WVK_AXIS_RTHUMB) {
        UpdateThumb(slot_index, timestamp, control,
                     GetThumbArea(
                        control == WVK_AXIS_LTHUMB ? state->Gamepad.sThumbLX : state->Gamepad.sThumbRX,
                        control == WVK_AXIS_LTHUMB ? state->Gamepad.sThumbLY : state->Gamepad.sThumbRY,
                        control == WVK_AXIS_LTHUMB ? &(vkStates->lThumbIsSquare) : &(vkStates->rThumbIsSquare),
                        vkStates->items[control].area),
                     vkStates, keystrokeQueue);
    }
}

void xiw_vk_Repeat(DWORD slot_index, XINPUTW_VK_STATES *vkStates, XINPUTW_KEYSTROKE_QUEUE * keystrokeQueue) {
    XINPUTW_VK_CONTROLNAME control;
    ULONGLONG timestamp;
    XINPUTW_KEYSTROKE keystroke;
    XINPUTW_VK_STATE * vkState;

    timestamp = GetTickCount64();

    /* Set default keystroke options */
    memset((void*)&keystroke, 0, sizeof(XINPUTW_KEYSTROKE));

    keystroke.keystroke.Flags = XINPUT_KEYSTROKE_KEYDOWN | XINPUT_KEYSTROKE_REPEAT;
    keystroke.keystroke.UserIndex = slot_index;

    for (control = WVK_BTN_A; control <= WVK_AXIS_RTHUMB; ++control) {
        vkState = &(vkStates->items[control]);
        keystroke.keystroke.VirtualKey = GetVirtualKey(control, vkState->area);

        while (keystroke.keystroke.VirtualKey != 0 && (
            (!vkState->isRepeat && timestamp - vkState->timestamp > REPEAT_DELAY_MS)
            || (vkState->isRepeat && timestamp - vkState->timestamp > REPEAT_PERIOD_MS)
            )) {
                vkState->timestamp += vkState->isRepeat ? REPEAT_PERIOD_MS : REPEAT_DELAY_MS;

                keystroke.timestamp = vkState->timestamp;
                xiw_vk_KeystrokeQueuePush(keystrokeQueue, &keystroke);

                vkState->isRepeat = TRUE;
        }
    }
}
