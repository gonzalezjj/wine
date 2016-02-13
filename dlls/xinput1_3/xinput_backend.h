/*
 * The Wine project - XInput Joystick Library - core<->backend interface
 *
 * Copyright 2016 Juan Gonzalez
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

#ifndef __WINE_DLLS_XINPUT_BACKEND_H
#define __WINE_DLLS_XINPUT_BACKEND_H


#include <stdint.h>
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "xinput.h"

#define XINPUTW_VAL_MIN (-0x8000)
#define XINPUTW_VAL_MAX (0x7fff)
typedef INT16 XINPUTW_VALUE;

/* Contains information about the rumble status of a device */
typedef struct _XINPUTW_DEV_RUMBLE {
    WORD hf;
    WORD lf;
} XINPUTW_DEV_RUMBLE;

typedef struct _XINPUTW_DEV_CAPABILITIES {
    /* Bitmap representing the available buttons. Set with xiw_util_SetCapabilitiesBtn */
    WORD buttons;

    /* Axis resolution bit count. set using xiw_util_SetCapabilitiesAxis */
    BYTE axes[6];

    /* Whether the slot supports rumble (force feedback) */
    BOOL has_rumble;
} XINPUTW_DEV_CAPABILITIES;

/* Defines the interface exposed by a backend to xinput_core */
typedef struct _XINPUTW_BACKEND {
    /*
     * Printable name of the backend
     */
    const char *Name;

    /*
     * Initialize the backend. Is called exactly once by xinput_core
     * during initialization, before calling anything else in this backend.
     * If this pointer is NULL, the backend will not be used
     */
    void (*Initialize)(void);

    /*
     * Try to connect a new device to the given slot.
     *
     * slot_index: The index of the free slot where a device could be connected
     * capabilities (out): If the function was successful, the capabilities structure should be
     *     filled by the backend
     *
     * Returns: TRUE if successful, FALSE if not (ie. if no new device is available)
     */
    BOOL (*TryConnectDevice)(DWORD target_slot_index, XINPUTW_DEV_CAPABILITIES *capabilities);

    /*
     * Close the device at the given slot. Called if access to the device fails, or when shutting down xinput.
     *
     * slot_index: The index of the slot to be disconnected
     */
    void (*DisconnectDevice)(DWORD slot_index);

    /*
     * Synchronize the gamepad state for a given slot.
     *
     * Returns: TRUE if successful (even for no-ops), FALSE if the device was disconnected
     */
    BOOL (*SyncKeyState)(DWORD slot_index);

    /*
     * Synchronize the gamepad battery for a given slot.
     *
     * slot_index: The index of the slot.
     * battery_level (out): The current battery level. If the battery state is unknown,
     *     battery_level should be set to a value less than 0 (eg. -1)
     *
     * Returns: TRUE if successful (even for no-ops), FALSE if the device was disconnected
     */
    BOOL (*SyncBatteryState)(DWORD slot_index, INT16 *battery_level);

    /*
     * Synchronize the gamepad battery for a given slot.
     *
     * slot_index: The index of the slot.
     * rumble: The rumble values to be set. The effect should continue until SetRumble is called again
     *
     * Returns: TRUE if successful (even for no-ops), FALSE if the device was disconnected
     */
    BOOL (*SetRumble)(DWORD slot_index, const XINPUTW_DEV_RUMBLE *rumble);

} XINPUTW_BACKEND;

typedef enum _XINPUTW_EVENT_CODE {
    WINE_BTN_A = 0ul,
    WINE_BTN_B,
    WINE_BTN_Y,
    WINE_BTN_X,
    WINE_BTN_START,
    WINE_BTN_BACK,
    WINE_BTN_LSHOULDER,
    WINE_BTN_RSHOULDER,
    WINE_BTN_LTHUMB,
    WINE_BTN_RTHUMB,
    WINE_BTN_DPAD_UP,
    WINE_BTN_DPAD_DOWN,
    WINE_BTN_DPAD_LEFT,
    WINE_BTN_DPAD_RIGHT,
    WINE_AXIS_LTRIGGER,
    WINE_AXIS_RTRIGGER,
    WINE_AXIS_LTHUMB_X,
    WINE_AXIS_LTHUMB_Y,
    WINE_AXIS_RTHUMB_X,
    WINE_AXIS_RTHUMB_Y,
    WINE_CONTROL_COUNT
} XINPUTW_EVENT_CODE;

#define WINE_BTN_MIN WINE_BTN_A
#define WINE_BTN_MAX WINE_BTN_DPAD_RIGHT
#define WINE_AXIS_MIN WINE_AXIS_LTRIGGER
#define WINE_AXIS_MAX WINE_AXIS_RTHUMB_Y

/**
 * Defines the condition under which a numeric input value is considered an "on"-state for a button
 */
typedef enum _VAL_TO_BTN_MAP {
    VAL_TO_BTN_NONE,
    VAL_TO_BTN_LT_ZERO,
    VAL_TO_BTN_LE_ZERO,
    VAL_TO_BTN_ZERO,
    VAL_TO_BTN_GT_ZERO,
    VAL_TO_BTN_GE_ZERO
} VAL_TO_BTN_MAP;

/**
 * Defines whether a numeric input value should be inverted when mapping it to an axis
 */
typedef enum _AXIS_MAP {
    AXIS_MAP_REGULAR,
    AXIS_MAP_INVERTED
} AXIS_MAP;

typedef union _XINPUTW_EVENT_MAP {
    VAL_TO_BTN_MAP button;
    AXIS_MAP axis;
} XINPUTW_EVENT_MAP;

typedef struct _XINPUTW_EVENT {
    XINPUTW_EVENT_CODE code;
    XINPUTW_VALUE value;
    XINPUTW_EVENT_MAP value_map;
    /* Timestamp as provided by GetTickCount64 */
    ULONGLONG timestamp;
} XINPUTW_EVENT;

/*
 * Notify xinput_core of a change in the gamepad state
 * slot_index: The index of the slot, as passed when the core calls get_new_device.
 * event: The event being pushed
 *
 * This function can be called asynchronously whenever an event occurs.
 */
void xiw_core_PushEvent(DWORD slot_index, const XINPUTW_EVENT *event);

#endif /* __WINE_DLLS_XINPUT_BACKEND_H */
