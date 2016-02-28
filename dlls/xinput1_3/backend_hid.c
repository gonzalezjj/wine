/*
 * The Wine project - XInput Joystick Library - HID backend
 *
 * Copyright 2016, Juan Gonzalez
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

#include "xinput_backend_hid.h"

WINE_DEFAULT_DEBUG_CHANNEL(xinputhid);

/* Exports */

static void Initialize(void)
{

}

static BOOL TryConnectDevice(DWORD target_slot_index, XINPUTW_DEV_CAPABILITIES *capabilities)
{
    FIXME("stub\n");

    return FALSE;
}

static void DisconnectDevice(DWORD slot_index)
{
    FIXME("stub\n");
}

static BOOL SyncKeyState(DWORD slot_index)
{
    FIXME("stub\n");

    return FALSE;
}

static BOOL SyncBatteryState(DWORD slot_index, INT16 *battery_level)
{
    FIXME("stub\n");

    return FALSE;
}

static BOOL SetRumble(DWORD slot_index, const XINPUTW_DEV_RUMBLE *rumble)
{
    FIXME("stub\n");

    return FALSE;
}

const XINPUTW_BACKEND xinput_backend_hid =
{
    "Wine XInput HID backend",
    &Initialize,
    &TryConnectDevice,
    &DisconnectDevice,
    &SyncKeyState,
    &SyncBatteryState,
    &SetRumble
};
