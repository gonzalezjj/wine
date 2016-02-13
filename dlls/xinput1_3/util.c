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

#include "config.h"

#include "xinput_util.h"

#include "wine/debug.h"

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winreg.h"

WINE_DEFAULT_DEBUG_CHANNEL(xinput);

static XINPUTW_VALUE RangeFindCenter(XINPUTW_VALUE min, XINPUTW_VALUE max) {
    /* This is a heuristic, but it should work most of the time */
    if(min == (-max) - 1 || min == -max) return 0;
    else return (XINPUTW_VALUE)((INT32)min + ((INT32)max + (INT32)min) / 2);
}

static XINPUTW_VALUE ConvToRange(XINPUTW_VALUE value, XINPUTW_VALUE source_min, XINPUTW_VALUE source_max,
                                 XINPUTW_VALUE target_min, XINPUTW_VALUE target_max) {
    INT32 value_internal, source_range, source_center, target_range, target_center;
    BOOL below_center;

    source_center = RangeFindCenter(source_min, source_max);
    target_center = RangeFindCenter(target_min, target_max);
    below_center = value <= source_center;

    /* account for assymetric ranges (using twos' complement) */
    value_internal = below_center ? (INT32)source_center - (INT32)value : (INT32)value - (INT32)source_center;
    source_range = below_center ? (INT32)source_center - (INT32)source_min : (INT32)source_max - (INT32)source_center;
    target_range = below_center ? (INT32)target_center - (INT32)target_min : (INT32)target_max - (INT32)target_center;

    if (source_range == 0 || target_range == 0) {
        value_internal = target_center;
    } else {
        /* scale and shift the value */
        value_internal = (value_internal * target_range) / source_range;
        value_internal = below_center ? target_center - value_internal : target_center + value_internal;
    }
    TRACE("value %d, source: [%d, %d], target: [%d, %d], result: %d\n", value, source_min, source_max,
        target_min, target_max, (XINPUTW_VALUE)value_internal);
    return (XINPUTW_VALUE)value_internal;
}

static BYTE GetRangeBitCount(XINPUTW_VALUE min, XINPUTW_VALUE max) {
    BYTE result;
    WORD range;

    result = 0;
    range = (WORD)((INT32)max - (INT32)min);

    while (range != 0) {
        ++result;
        range = range >> 1;
    }

    return result;
}


XINPUTW_VALUE xiw_util_ConvToXIWValue(XINPUTW_VALUE value, XINPUTW_VALUE range_min, XINPUTW_VALUE range_max) {
    return ConvToRange(value, range_min, range_max, XINPUTW_VAL_MIN, XINPUTW_VAL_MAX);
}

XINPUTW_VALUE xiw_util_ConvFromXIWValue(XINPUTW_VALUE value, XINPUTW_VALUE range_min, XINPUTW_VALUE range_max) {
    return ConvToRange(value, XINPUTW_VAL_MIN, XINPUTW_VAL_MAX, range_min, range_max);
}

void xiw_util_SetCapabilitiesBtn(WORD *buttons, XINPUTW_EVENT_CODE code, BOOL value) {
    WORD mask;

    if (code < WINE_BTN_A || code > WINE_BTN_DPAD_RIGHT) return;
    mask = 1ul << code;
    *buttons = (*buttons & ~mask) | (value ? mask : 0);
}

void xiw_util_SetCapabilitiesAxis(BYTE *axes, XINPUTW_EVENT_CODE code, XINPUTW_VALUE min, XINPUTW_VALUE max) {
    if (code < WINE_AXIS_LTRIGGER || code > WINE_AXIS_RTHUMB_Y) return;
    axes[code - WINE_AXIS_LTRIGGER] = GetRangeBitCount(min, max);
}


/*
 * Taken and adapted from dlls/dinput/device.c
 */
BOOL xiw_util_OpenCfgKeys(HKEY *defkey, HKEY *appkey, const char *subkey_path) {
    char buffer[MAX_PATH+16];
    DWORD len;

    *appkey = 0;

    /* Wine registry key: HKCU\Software\Wine\XInput */
    strcpy(buffer, "Software\\Wine\\XInput");
    if (subkey_path != NULL) {
        if (subkey_path[0] == '\\') {
            strcat(buffer, subkey_path);
        } else {
            strcat(buffer, "\\");
            strcat(buffer, subkey_path);
        }
    }
    if (RegOpenKeyA(HKEY_CURRENT_USER, buffer, defkey) != ERROR_SUCCESS)
        *defkey = 0;

    len = GetModuleFileNameA(0, buffer, MAX_PATH);
    if (len && len < MAX_PATH) {
        HKEY tmpkey;

        /* Wine registry key: HKCU\Software\Wine\AppDefaults\app.exe\XInput */
        if (RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\AppDefaults", &tmpkey) == ERROR_SUCCESS) {
            char *p, *appname = buffer;
            if ((p = strrchr(appname, '/'))) appname = p + 1;
            if ((p = strrchr(appname, '\\'))) appname = p + 1;
            strcat(appname, "\\XInput");
            if (subkey_path != NULL) {
                if (subkey_path[0] == '\\') {
                    strcat(buffer, subkey_path);
                } else {
                    strcat(buffer, "\\");
                    strcat(buffer, subkey_path);
                }
            }

            if (RegOpenKeyA(tmpkey, appname, appkey) != ERROR_SUCCESS)
                *appkey = 0;
            RegCloseKey(tmpkey);
        }
    }

    return *defkey || *appkey;
}

/*
 * Taken and adapted from dlls/dinput/device.c
 */
LONG xiw_util_GetCfgValueGeneric(HKEY defkey, HKEY appkey, const char *name, BYTE *buffer, DWORD size, DWORD *type) {
    LONG rc;

    /* Try to load the app-specific key */
    if (appkey) {
        /* If the key was not found, try the default. On any other return code, return that */
        if ((rc = RegQueryValueExA(appkey, name, NULL, type, (LPBYTE)buffer, &size)) != ERROR_FILE_NOT_FOUND)
            return rc;
    }

    if (defkey)
        return RegQueryValueExA(defkey, name, NULL, type, (LPBYTE)buffer, &size);

    return ERROR_FILE_NOT_FOUND;
}

DWORD xiw_util_GetCfgValueDW(HKEY defkey, HKEY appkey, const char *name, DWORD defaultValue) {
    DWORD key_type, value;
    LONG rc;
    rc = xiw_util_GetCfgValueGeneric(defkey, appkey, "ValueToButtonEpsilon",
        (BYTE *)&value, sizeof(DWORD), &key_type);
    if (rc == ERROR_SUCCESS && key_type == REG_DWORD)
        return value;

    return defaultValue;
}
