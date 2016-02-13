/*
 * The Wine project - XInput Joystick Library - utility methods
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

#ifndef __WINE_DLLS_XINPUT_UTIL_H
#define __WINE_DLLS_XINPUT_UTIL_H

#include "xinput_backend.h"

XINPUTW_VALUE xiw_util_ConvToXIWValue(XINPUTW_VALUE value, XINPUTW_VALUE range_min, XINPUTW_VALUE range_max);
XINPUTW_VALUE xiw_util_ConvFromXIWValue(XINPUTW_VALUE value, XINPUTW_VALUE range_min, XINPUTW_VALUE range_max);

void xiw_util_SetCapabilitiesAxis(uint8_t *axes, XINPUTW_EVENT_CODE code, XINPUTW_VALUE min, XINPUTW_VALUE max);
void xiw_util_SetCapabilitiesBtn(WORD *buttons, XINPUTW_EVENT_CODE code, BOOL value);


/*
 * Get the default and the app-specific config keys.
 *
 * defkey (out): The default (ie. global) xinput config key
 * appkey (out): The app-specific xinput config key
 * subkey_path: Fetch a specific key under the XInput root keys. Can be set to NULL
 *
 * Taken and adapted from dlls/dinput/device.c
 */
BOOL xiw_util_OpenCfgKeys(HKEY *defkey, HKEY *appkey, const char *subkey_path);

/*
 * Get a config value from an app-specific registry key with a default fallback.
 *
 * defkey: The default root key
 * appkey: The app-specific root key
 * name: The value name under the specified keys
 * buffer (out): The buffer where the value will be saved
 * size: Specifies the buffer size
 * type (out): Returns the found config value type according to the RegQueryValueEx spec
 *
 * Returns: ERROR_SUCCESS on success
 */
LONG xiw_util_GetCfgValueGeneric(HKEY defkey, HKEY appkey, const char *name, BYTE *buffer, DWORD size, DWORD *type);


/*
 * Get a DWORD config value from an app-specific registry key or default key, or set it to a default value.
 *
 * defkey: The default root key
 * appkey: The app-specific root key
 * name: The value name under the specified keys
 *
 * Returns: The requested value or the default if it could not be found
 */
DWORD xiw_util_GetCfgValueDW(HKEY defkey, HKEY appkey, const char *name, DWORD defaultValue);


#endif /* __WINE_DLLS_XINPUT_UTIL_H */
