/*
 * The Wine project - XInput Joystick Library
 *
 * Copyright 2008 Andrew Fenn
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

#include "config.h"

#include "wine/debug.h"

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "xinput.h"

#include "xinput_backend.h"
#include "xinput_util.h"
#include "xinput_core.h"

WINE_DEFAULT_DEBUG_CHANNEL(xinput);


typedef struct _XINPUTW_SLOT {
    DWORD slot_index;

    XINPUT_STATE state;
    BOOL state_has_changes;
    XINPUT_VIBRATION rumble_state;

    XINPUTW_VK_STATES vkStates;
    XINPUTW_KEYSTROKE_QUEUE keystrokes;
    INT16 battery_level;
    XINPUTW_DEV_CAPABILITIES capabilities;

    const XINPUTW_BACKEND *backend;

    CRITICAL_SECTION cs_device;
    CRITICAL_SECTION cs_status;
} XINPUTW_SLOT;

/*
 * NULL-Terminated array of pointers to backends.
 */
const XINPUTW_BACKEND *xinput_backends[] = {
    NULL
};

/* The epsilon / deadzone to consider a button pressed. */
#define DEFAULT_VAL_TO_BTN_EPSILON (XINPUTW_VAL_MAX / 4)
static XINPUTW_VALUE val_to_btn_epsilon;

static BOOL is_core_initialized;
static CRITICAL_SECTION cs_core_initalization;
static BOOL is_xinput_enabled;
static XINPUTW_SLOT slots[XUSER_MAX_COUNT];


/* Invert the value of an axis while avoiding overflows */
static XINPUTW_VALUE InvertXIWValue(XINPUTW_VALUE value) {
    TRACE("value: %d\n", value);
    if (value == XINPUTW_VAL_MIN) return XINPUTW_VAL_MAX;
    if (value == XINPUTW_VAL_MAX) return XINPUTW_VAL_MIN;
    return -value;
}

static void slot_close(DWORD slot) {
    if (slots[slot].backend == NULL) return;

    EnterCriticalSection(&slots[slot].cs_device);

    (slots[slot].backend->DisconnectDevice)(slot);
    slots[slot].backend = NULL;

    memset((void *)&slots[slot].state, 0, sizeof(slots[slot].state));
    slots[slot].state_has_changes = FALSE;
    memset((void *)&slots[slot].rumble_state, 0, sizeof(slots[slot].rumble_state));
    memset((void *)&slots[slot].vkStates, 0, sizeof(slots[slot].vkStates));
    xiw_vk_KeystrokeQueueClear(&slots[slot].keystrokes);
    slots[slot].battery_level = -1;
    memset((void *)&slots[slot].capabilities, 0, sizeof(slots[slot].capabilities));

    LeaveCriticalSection(&slots[slot].cs_device);
}

static void ParseAxisEvent(DWORD slot, const XINPUTW_EVENT *event) {
    XINPUTW_VALUE target_value;
    BYTE *trigger_val;
    SHORT *thumb_val;

    TRACE("slot %d, input code %d, value %d\n", slot, event->code, event->value);

    trigger_val = NULL;
    thumb_val = NULL;

    switch (event->code) {
        case WINE_AXIS_LTRIGGER:
            trigger_val = &slots[slot].state.Gamepad.bLeftTrigger;
            break;
        case WINE_AXIS_RTRIGGER:
            trigger_val = &slots[slot].state.Gamepad.bRightTrigger;
            break;
        case WINE_AXIS_LTHUMB_X:
            thumb_val = &slots[slot].state.Gamepad.sThumbLX;
            break;
        case WINE_AXIS_LTHUMB_Y:
            thumb_val = &slots[slot].state.Gamepad.sThumbLY;
            break;
        case WINE_AXIS_RTHUMB_X:
            thumb_val = &slots[slot].state.Gamepad.sThumbRX;
            break;
        case WINE_AXIS_RTHUMB_Y:
            thumb_val = &slots[slot].state.Gamepad.sThumbRY;
            break;
        default:
            WARN("invalid code %d\n", event->code);
            return;
    }

    if (trigger_val != NULL) {
        target_value = xiw_util_ConvFromXIWValue(
            event->value_map.axis == AXIS_MAP_INVERTED ? InvertXIWValue(event->value) : event->value,
            0, 255);
        if(*trigger_val != (BYTE)target_value) {
            /* Update the state for XInputGetState */
            *trigger_val = (BYTE)target_value;
            slots[slot].state_has_changes = TRUE;

            /* Post a new event for XInputGetKeystroke if necessary */
            xiw_vk_Update(slot, event->timestamp, event->code, &slots[slot].state, &slots[slot].vkStates, &slots[slot].keystrokes);
        }
    } else {
        target_value = xiw_util_ConvFromXIWValue(
            event->value_map.axis == AXIS_MAP_INVERTED ? InvertXIWValue(event->value) : event->value,
            -32768, 32767);
        if(*thumb_val != (WORD)target_value) {
            /* Update the state for XInputGetState */
            *thumb_val = (WORD)target_value;
            slots[slot].state_has_changes = TRUE;

            /* Post a new event for XInputGetKeystroke if necessary */
            xiw_vk_Update(slot, event->timestamp, event->code, &slots[slot].state, &slots[slot].vkStates, &slots[slot].keystrokes);
        }
    }
}

static void ParseBtnEvent(DWORD slot, const XINPUTW_EVENT *event) {
    WORD bit_mask;

    BOOL btn_is_pressed;
    WORD masked_value;

    TRACE("slot %d, input code %d, value %d\n", slot, event->code, event->value);

    switch (event->code) {
        case WINE_BTN_A:
            bit_mask = XINPUT_GAMEPAD_A;
            break;
        case WINE_BTN_B:
            bit_mask = XINPUT_GAMEPAD_B;
            break;
        case WINE_BTN_X:
            bit_mask = XINPUT_GAMEPAD_X;
            break;
        case WINE_BTN_Y:
            bit_mask = XINPUT_GAMEPAD_Y;
            break;
        case WINE_BTN_START:
            bit_mask = XINPUT_GAMEPAD_START;
            break;
        case WINE_BTN_BACK:
            bit_mask = XINPUT_GAMEPAD_BACK;
            break;
        case WINE_BTN_LSHOULDER:
            bit_mask = XINPUT_GAMEPAD_LEFT_SHOULDER;
            break;
        case WINE_BTN_RSHOULDER:
            bit_mask = XINPUT_GAMEPAD_RIGHT_SHOULDER;
            break;
        case WINE_BTN_LTHUMB:
            bit_mask = XINPUT_GAMEPAD_LEFT_THUMB;
            break;
        case WINE_BTN_RTHUMB:
            bit_mask = XINPUT_GAMEPAD_RIGHT_THUMB;
            break;
        case WINE_BTN_DPAD_UP:
            bit_mask = XINPUT_GAMEPAD_DPAD_UP;
            break;
        case WINE_BTN_DPAD_DOWN:
            bit_mask = XINPUT_GAMEPAD_DPAD_DOWN;
            break;
        case WINE_BTN_DPAD_LEFT:
            bit_mask = XINPUT_GAMEPAD_DPAD_LEFT;
            break;
        case WINE_BTN_DPAD_RIGHT:
            bit_mask = XINPUT_GAMEPAD_DPAD_RIGHT;
            break;
        default:
            WARN("invalid xinput_code %d\n", event->code);
            return;
    }

    switch (event->value_map.button) {
        case VAL_TO_BTN_LT_ZERO:
            btn_is_pressed = event->value < -val_to_btn_epsilon;
            break;
        case VAL_TO_BTN_LE_ZERO:
            btn_is_pressed = event->value <= val_to_btn_epsilon;
            break;
        case VAL_TO_BTN_ZERO:
            btn_is_pressed = event->value >= -val_to_btn_epsilon && event->value <= val_to_btn_epsilon;
            break;
        case VAL_TO_BTN_GT_ZERO:
            btn_is_pressed = event->value > val_to_btn_epsilon;
            break;
        case VAL_TO_BTN_GE_ZERO:
            btn_is_pressed = event->value >= -val_to_btn_epsilon;
            break;
        default:
            WARN("invalid button map %d\n", event->value_map.button);
            return;
    }

    masked_value = (btn_is_pressed ? ~((WORD)0) : 0) & bit_mask;
    if ((slots[slot].state.Gamepad.wButtons & bit_mask) != masked_value) {
        /* Update the state for XInputGetState */
        slots[slot].state.Gamepad.wButtons = (slots[slot].state.Gamepad.wButtons & ~bit_mask) | masked_value;
        slots[slot].state_has_changes = TRUE;

        /* Post a new event for XInputGetKeystroke if necessary */
        xiw_vk_Update(slot, event->timestamp, event->code, &slots[slot].state, &slots[slot].vkStates, &slots[slot].keystrokes);
    }
}

static BOOL TryConnectDevice(DWORD target_slot_index) {
    const XINPUTW_BACKEND **backend;

    TRACE("slot %hu\n", target_slot_index);

    EnterCriticalSection(&slots[target_slot_index].cs_device);

    if (slots[target_slot_index].backend != NULL) {
        LeaveCriticalSection(&slots[target_slot_index].cs_device);
        return TRUE;
    }

    for (backend = xinput_backends; (*backend) != NULL; ++backend) {
        if ((*backend)->Initialize == NULL) {
            TRACE("skipping disabled backend %s\n", (*backend)->Name);
            continue;
        }

        if ((*backend)->TryConnectDevice(target_slot_index, &slots[target_slot_index].capabilities)) {
            TRACE("successfully connected slot %d from backend %s\n", target_slot_index, (*backend)->Name);
            slots[target_slot_index].backend = *backend;

            LeaveCriticalSection(&slots[target_slot_index].cs_device);
            return TRUE;
        }
        TRACE("could not connect slot %d from backend %s\n", target_slot_index, (*backend)->Name);
    }

    LeaveCriticalSection(&slots[target_slot_index].cs_device);
    return FALSE;
}

static WORD GetCapabilitiesBtnFlag(XINPUTW_EVENT_CODE code) {
    switch (code) {
        case WINE_BTN_A:
            return XINPUT_GAMEPAD_A;
        case WINE_BTN_B:
            return XINPUT_GAMEPAD_B;
        case WINE_BTN_Y:
            return XINPUT_GAMEPAD_Y;
        case WINE_BTN_X:
            return XINPUT_GAMEPAD_X;
        case WINE_BTN_START:
            return XINPUT_GAMEPAD_START;
        case WINE_BTN_BACK:
            return XINPUT_GAMEPAD_BACK;
        case WINE_BTN_LSHOULDER:
            return XINPUT_GAMEPAD_LEFT_THUMB;
        case WINE_BTN_RSHOULDER:
            return XINPUT_GAMEPAD_RIGHT_THUMB;
        case WINE_BTN_LTHUMB:
            return XINPUT_GAMEPAD_LEFT_SHOULDER;
        case WINE_BTN_RTHUMB:
            return XINPUT_GAMEPAD_RIGHT_SHOULDER;
        case WINE_BTN_DPAD_UP:
            return XINPUT_GAMEPAD_DPAD_UP;
        case WINE_BTN_DPAD_DOWN:
            return XINPUT_GAMEPAD_DPAD_DOWN;
        case WINE_BTN_DPAD_LEFT:
            return XINPUT_GAMEPAD_DPAD_LEFT;
        case WINE_BTN_DPAD_RIGHT:
            return XINPUT_GAMEPAD_DPAD_RIGHT;
        default:
            return 0;
    }
}

static WORD GetResolutionBitmap(BYTE dev_max_bits, BYTE target_byte_size) {
    WORD max_bits, result;

    max_bits = (WORD)target_byte_size * 8;
    if (dev_max_bits < max_bits)
        max_bits = (WORD)dev_max_bits;

    result = (WORD)((1ul << (max_bits + 1)) - 1);
    TRACE("dev_max_bits %uh, target_byte_size %uh\n", dev_max_bits, target_byte_size);

    return result;
}

static void EnsureInitialized(void) {
    unsigned int i;
    const XINPUTW_BACKEND **backend;
    HKEY defkey, appkey;

    EnterCriticalSection(&cs_core_initalization);
    if (!is_core_initialized) {
        TRACE("initializing core\n");

        is_xinput_enabled = TRUE;

        xiw_util_OpenCfgKeys(&defkey, &appkey, NULL);
        val_to_btn_epsilon = (XINPUTW_VALUE) xiw_util_GetCfgValueDW(defkey, appkey,
            "ValueToButtonEpsilon", DEFAULT_VAL_TO_BTN_EPSILON);

        for (i = 0; i < XUSER_MAX_COUNT; i++) {
            memset((void *)&slots[i], 0, sizeof(slots[i]));

            InitializeCriticalSection(&slots[i].cs_device);
            InitializeCriticalSection(&slots[i].cs_status);
        }

        for (backend = xinput_backends; *backend != NULL; ++backend) {
            if ((*backend)->Initialize == NULL) {
                TRACE("skipping disabled backend %s\n", (*backend)->Name);
                continue;
            }

            TRACE("initializing backend %s\n", (*backend)->Name);
            (*(*backend)->Initialize)();
        }

        is_core_initialized = TRUE;
    }
    LeaveCriticalSection(&cs_core_initalization);
}

/*
 * Notify xinput_core of a change in the gamepad state
 * slot_index: The index of the slot, as passed when the core calls get_new_device.
 * event: The event being pushed
 *
 * This function can be called asynchronously whenever an event occurs.
 */
void xiw_core_PushEvent(DWORD slot_index, const XINPUTW_EVENT *event) {
    TRACE("slot %hu\n", slot_index);

    EnsureInitialized();

    if (slot_index > XUSER_MAX_COUNT)
        return;

    EnterCriticalSection(&slots[slot_index].cs_status);

    if (event->code >= WINE_BTN_MIN && event->code <= WINE_BTN_MAX)
        ParseBtnEvent(slot_index, event);
     else if (event->code >= WINE_AXIS_MIN && event->code <= WINE_AXIS_MAX)
        ParseAxisEvent(slot_index, event);
    else
        WARN("invalid xinput_code %d\n", event->code);

    LeaveCriticalSection(&slots[slot_index].cs_status);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    DWORD i;

    switch(reason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(inst);
            is_core_initialized = FALSE;
            InitializeCriticalSection(&cs_core_initalization);
            break;
        case DLL_PROCESS_DETACH:
            for (i = 0; i < XUSER_MAX_COUNT; ++i) {
                slot_close(i);
                DeleteCriticalSection(&slots[i].cs_device);
                DeleteCriticalSection(&slots[i].cs_status);
            }
            DeleteCriticalSection(&cs_core_initalization);
            break;
    }

    return TRUE;
}

void WINAPI XInputEnable(BOOL enable)
{
    DWORD i;
    XINPUTW_DEV_RUMBLE rumble;

    TRACE("xinput %s\n", enable ? "enabled" : "disabled");

    EnsureInitialized();

    is_xinput_enabled = enable;
    for (i = 0; i < XUSER_MAX_COUNT; ++i) {
        EnterCriticalSection(&slots[i].cs_device);
        if (slots[i].backend != NULL) {
            rumble.hf = is_xinput_enabled ? slots[i].rumble_state.wLeftMotorSpeed : 0;
            rumble.lf = is_xinput_enabled ? slots[i].rumble_state.wRightMotorSpeed : 0;
            if (!(slots[i].backend->SetRumble)(i, &rumble)) {
                slot_close(i);
            }
        }
        LeaveCriticalSection(&slots[i].cs_device);
    }
}

DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
{
    XINPUTW_DEV_RUMBLE rumble;

    TRACE("slot %hu\n", dwUserIndex);

    EnsureInitialized();

    if (dwUserIndex < XUSER_MAX_COUNT)
    {
        EnterCriticalSection(&slots[dwUserIndex].cs_device);
        if (!TryConnectDevice(dwUserIndex)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        EnterCriticalSection(&slots[dwUserIndex].cs_status);

        memcpy((void *)&slots[dwUserIndex].rumble_state, (void *)pVibration,
            sizeof(slots[dwUserIndex].rumble_state));

        if (is_xinput_enabled) {
            rumble.hf = pVibration->wLeftMotorSpeed;
            rumble.lf = pVibration->wRightMotorSpeed;
            if (!(slots[dwUserIndex].backend->SetRumble)(dwUserIndex, &rumble)) {
                LeaveCriticalSection(&slots[dwUserIndex].cs_status);
                slot_close(dwUserIndex);
                LeaveCriticalSection(&slots[dwUserIndex].cs_device);
                return ERROR_DEVICE_NOT_CONNECTED;
            }
        }

        LeaveCriticalSection(&slots[dwUserIndex].cs_status);
        LeaveCriticalSection(&slots[dwUserIndex].cs_device);
        return ERROR_SUCCESS;
    }
    return ERROR_BAD_ARGUMENTS;
}

DWORD WINAPI DECLSPEC_HOTPATCH XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    TRACE("slot %hu\n", dwUserIndex);

    EnsureInitialized();

    if (dwUserIndex < XUSER_MAX_COUNT)
    {
        if (!is_xinput_enabled) {
            memset((void *)pState, 0, sizeof(*pState));
            return ERROR_SUCCESS;
        }

        EnterCriticalSection(&slots[dwUserIndex].cs_device);
        if (!TryConnectDevice(dwUserIndex)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        EnterCriticalSection(&slots[dwUserIndex].cs_status);


        if (!(slots[dwUserIndex].backend->SyncKeyState)(dwUserIndex)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_status);
            slot_close(dwUserIndex);
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        if (slots[dwUserIndex].state_has_changes) {
            ++slots[dwUserIndex].state.dwPacketNumber;
            slots[dwUserIndex].state_has_changes = FALSE;
        }

        memcpy((void *)pState, (const void *)&slots[dwUserIndex].state, sizeof(slots[dwUserIndex].state));

        LeaveCriticalSection(&slots[dwUserIndex].cs_status);
        LeaveCriticalSection(&slots[dwUserIndex].cs_device);

        return ERROR_SUCCESS;
    }
    return ERROR_BAD_ARGUMENTS;
}

DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserve, PXINPUT_KEYSTROKE pKeystroke)
{
    int earliest_event_slot;
    ULONGLONG earliest_timestamp;
    DWORD i;
    const XINPUTW_KEYSTROKE *keystroke;
    DWORD result;

    TRACE("slot %hu\n", dwUserIndex);

    EnsureInitialized();

    if (dwUserIndex < XUSER_MAX_COUNT)
    {
        if (!is_xinput_enabled) return ERROR_EMPTY;

        EnterCriticalSection(&slots[dwUserIndex].cs_device);
        if (!TryConnectDevice(dwUserIndex)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        EnterCriticalSection(&slots[dwUserIndex].cs_status);

        if (!(slots[dwUserIndex].backend->SyncKeyState)(dwUserIndex)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_status);
            slot_close(dwUserIndex);
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        /* Send REPEAT keystrokes if necessary */
        xiw_vk_Repeat(dwUserIndex, &slots[dwUserIndex].vkStates, &slots[dwUserIndex].keystrokes);

        if ((keystroke = xiw_vk_KeystrokeQueueGetFront(&slots[dwUserIndex].keystrokes)) != NULL) {
            memcpy((void *)pKeystroke, (void *)&keystroke->keystroke, sizeof(XINPUT_KEYSTROKE));
            xiw_vk_KeystrokeQueuePop(&slots[dwUserIndex].keystrokes);
            result = ERROR_SUCCESS;
        } else {
            result = ERROR_EMPTY;
        }

        LeaveCriticalSection(&slots[dwUserIndex].cs_status);
        LeaveCriticalSection(&slots[dwUserIndex].cs_device);
        return result;

    } else if (dwUserIndex == XUSER_INDEX_ANY) {
        if (!is_xinput_enabled) return ERROR_EMPTY;
        earliest_event_slot = -1;

        for (i = 0; i < XUSER_MAX_COUNT; i++) {
            TRACE("Checking slot %d\n", i);
            EnterCriticalSection(&slots[i].cs_device);
            if (!TryConnectDevice(i)) {
                LeaveCriticalSection(&slots[i].cs_device);
                continue;
            }

            /* Grab the current slot's critical sections */
            EnterCriticalSection(&slots[i].cs_status);
            if (!(slots[i].backend->SyncKeyState)(i)) {
                LeaveCriticalSection(&slots[i].cs_status);
                slot_close(i);
                LeaveCriticalSection(&slots[i].cs_device);
                continue;
            }

            /* Send REPEAT keystrokes if necessary */
            xiw_vk_Repeat(i, &slots[i].vkStates, &slots[i].keystrokes);

            if ((keystroke = xiw_vk_KeystrokeQueueGetFront(&slots[i].keystrokes)) != NULL
                && (earliest_event_slot == -1 || earliest_timestamp > keystroke->timestamp)) {
                /* Only keep the relevant slot's critical section, release the previous one */
                if (earliest_event_slot != -1) {
                    TRACE("Closing previous slot %d\n", earliest_event_slot);
                    LeaveCriticalSection(&slots[earliest_event_slot].cs_status);
                    LeaveCriticalSection(&slots[earliest_event_slot].cs_device);
                }
                TRACE("New top slot is %d\n", i);

                earliest_event_slot = i;
                earliest_timestamp = keystroke->timestamp;
            } else {
                TRACE("Closing slot %d\n", i);
                LeaveCriticalSection(&slots[i].cs_status);
                LeaveCriticalSection(&slots[i].cs_device);
            }
        }

        if (earliest_event_slot != -1) {
            TRACE("Closing slot %d\n", earliest_event_slot);
            keystroke = xiw_vk_KeystrokeQueueGetFront(&slots[earliest_event_slot].keystrokes);
            memcpy((void *)pKeystroke, (void *)&keystroke->keystroke, sizeof(XINPUT_KEYSTROKE));
            xiw_vk_KeystrokeQueuePop(&slots[earliest_event_slot].keystrokes);

            /* Every other slot's critical sections were already released */
            LeaveCriticalSection(&slots[earliest_event_slot].cs_status);
            LeaveCriticalSection(&slots[earliest_event_slot].cs_device);

            return ERROR_SUCCESS;
        }

        return ERROR_EMPTY;
    }
    return ERROR_BAD_ARGUMENTS;
}

DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities)
{
    XINPUTW_DEV_CAPABILITIES *capabilities;
    int i;

    TRACE("slot %hu\n", dwUserIndex);

    EnsureInitialized();

    if (dwUserIndex < XUSER_MAX_COUNT)
    {
        EnterCriticalSection(&slots[dwUserIndex].cs_device);
        if (!TryConnectDevice(dwUserIndex)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        LeaveCriticalSection(&slots[dwUserIndex].cs_device);

        capabilities = &slots[dwUserIndex].capabilities;

        pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
        /* FIXME: Actually check the subtype (based on the available buttons) */
        pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;

        /* Set flags to zero. FF capabilities are checked further down */
        pCapabilities->Flags = 0;

        pCapabilities->Gamepad.wButtons = 0;
        /* Set button info */
        for (i = WINE_BTN_MIN; i <= WINE_BTN_MAX; ++i) {
            if (capabilities->buttons & (1 << (i - WINE_BTN_MIN)))
                pCapabilities->Gamepad.wButtons |= GetCapabilitiesBtnFlag(i);
        }

        /* Set axis info */
        pCapabilities->Gamepad.bLeftTrigger = (BYTE)GetResolutionBitmap(
            capabilities->axes[WINE_AXIS_LTRIGGER - WINE_AXIS_MIN],
            sizeof(pCapabilities->Gamepad.bLeftTrigger));
        pCapabilities->Gamepad.bRightTrigger = (BYTE)GetResolutionBitmap(
            capabilities->axes[WINE_AXIS_RTRIGGER - WINE_AXIS_MIN],
            sizeof(pCapabilities->Gamepad.bRightTrigger));
        pCapabilities->Gamepad.sThumbLX = (WORD)GetResolutionBitmap(
            capabilities->axes[WINE_AXIS_LTHUMB_X - WINE_AXIS_MIN],
            sizeof(pCapabilities->Gamepad.sThumbLX));
        pCapabilities->Gamepad.sThumbLY = (WORD)GetResolutionBitmap(
            capabilities->axes[WINE_AXIS_LTHUMB_Y - WINE_AXIS_MIN],
            sizeof(pCapabilities->Gamepad.sThumbLY));
        pCapabilities->Gamepad.sThumbRX = (WORD)GetResolutionBitmap(
            capabilities->axes[WINE_AXIS_RTHUMB_X - WINE_AXIS_MIN],
            sizeof(pCapabilities->Gamepad.sThumbRX));
        pCapabilities->Gamepad.sThumbRY = (WORD)GetResolutionBitmap(
            capabilities->axes[WINE_AXIS_RTHUMB_Y - WINE_AXIS_MIN],
            sizeof(pCapabilities->Gamepad.sThumbRY));

        /* Check force feedback capabilities */
        if (capabilities->has_rumble) {
            /* FIXME: XINPUT_CAPS_FFB_SUPPORTED is not defined */
            /* pCapabilities->Flags |= XINPUT_CAPS_FFB_SUPPORTED; */

            /* Evdev uses 16bit numbers to describe the rumble motor speed. No need to check */
            pCapabilities->Vibration.wLeftMotorSpeed = 0xffff;
            pCapabilities->Vibration.wRightMotorSpeed = 0xffff;
        } else {
            pCapabilities->Vibration.wLeftMotorSpeed = 0;
            pCapabilities->Vibration.wRightMotorSpeed = 0;
        }


        return ERROR_SUCCESS;
    }
    return ERROR_BAD_ARGUMENTS;
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD dwUserIndex, GUID* pDSoundRenderGuid, GUID* pDSoundCaptureGuid)
{
    FIXME("(%d %p %p) Stub!\n", dwUserIndex, pDSoundRenderGuid, pDSoundCaptureGuid);

    EnsureInitialized();

    if (dwUserIndex < XUSER_MAX_COUNT)
    {
        EnterCriticalSection(&slots[dwUserIndex].cs_device);
        if (!TryConnectDevice(dwUserIndex)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        LeaveCriticalSection(&slots[dwUserIndex].cs_device);

        /* Audio not supported (yet?) */
        *pDSoundRenderGuid = GUID_NULL;
        *pDSoundCaptureGuid = GUID_NULL;

        return ERROR_SUCCESS;
        /* If controller exists then return ERROR_SUCCESS */
    }
    return ERROR_BAD_ARGUMENTS;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE deviceType, XINPUT_BATTERY_INFORMATION* pBatteryInfo)
{
    TRACE("slot %hu\n", dwUserIndex);

    EnsureInitialized();

    if (dwUserIndex < XUSER_MAX_COUNT)
    {
        EnterCriticalSection(&slots[dwUserIndex].cs_device);
        if (!TryConnectDevice(dwUserIndex)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        EnterCriticalSection(&slots[dwUserIndex].cs_status);

        if (!(slots[dwUserIndex].backend->SyncBatteryState)(dwUserIndex, &slots[dwUserIndex].battery_level)) {
            LeaveCriticalSection(&slots[dwUserIndex].cs_status);
            slot_close(dwUserIndex);
            LeaveCriticalSection(&slots[dwUserIndex].cs_device);
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        if (slots[dwUserIndex].battery_level < 0) {
            pBatteryInfo->BatteryType = BATTERY_TYPE_UNKNOWN;
            pBatteryInfo->BatteryLevel = BATTERY_LEVEL_FULL;
        } else {
            pBatteryInfo->BatteryType = BATTERY_TYPE_ALKALINE;
            if (slots[dwUserIndex].battery_level < 0x2000)
                pBatteryInfo->BatteryLevel = BATTERY_LEVEL_EMPTY;
            else if (slots[dwUserIndex].battery_level < 0x4000)
                pBatteryInfo->BatteryLevel = BATTERY_LEVEL_LOW;
            else if (slots[dwUserIndex].battery_level < 0x6000)
                pBatteryInfo->BatteryLevel = BATTERY_LEVEL_MEDIUM;
            else
                pBatteryInfo->BatteryLevel = BATTERY_LEVEL_FULL;
        }

        LeaveCriticalSection(&slots[dwUserIndex].cs_status);
        LeaveCriticalSection(&slots[dwUserIndex].cs_device);
        return ERROR_SUCCESS;
    }
    return ERROR_BAD_ARGUMENTS;
}
