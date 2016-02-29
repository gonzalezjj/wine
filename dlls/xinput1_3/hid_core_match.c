/*
 * The Wine project - XInput Joystick Library - HID backend mapping core
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

#define USE_WS_PREFIX

#include "config.h"

#include <stdarg.h>
#include <string.h>

#include "wine/debug.h"

#include "windef.h"
#include "winbase.h"
#include "winerror.h"



#ifndef WINE_NTSTATUS_DECLARED
#define WINE_NTSTATUS_DECLARED
typedef LONG NTSTATUS;
#endif

#include "ddk/hidsdi.h"

#include "xinput_hid_mapping.h"

WINE_DEFAULT_DEBUG_CHANNEL(xinputhid);

static BOOL IsMatch_Item(HANDLE hid_device, XHID_MATCH_TYPE type, int index, const void * elements);

static BOOL IsMatch_BASEATTR(HANDLE hid_device, const XHID_BASEATTR_MATCH * element)
{
    HIDD_ATTRIBUTES attr;
    WCHAR buffer[1000];
    if (element->flags & XHID_ATTR_VID
        || element->flags & XHID_ATTR_PID
        || element->flags & XHID_ATTR_VERSION)
    {
        attr.Size = sizeof(HIDD_ATTRIBUTES);
        HidD_GetAttributes(hid_device, &attr);

        if ((element->flags & XHID_ATTR_VID && element->vid != attr.VendorID)
            || (element->flags & XHID_ATTR_PID && element->vid != attr.ProductID)
            || (element->flags & XHID_ATTR_VERSION && element->vid != attr.VersionNumber))
        {
            return FALSE;
        }
    }
    if (element->flags & XHID_ATTR_PRODSTR &&
        (element->product == NULL
        || !HidD_GetProductString(hid_device, (VOID*)buffer, sizeof(buffer))
        || wcscmp(element->product, buffer) != 0))
    {
        return FALSE;
    }

    if (element->flags & XHID_ATTR_MANUFSTR &&
        (element->manufacturer == NULL
        || !HidD_GetManufacturerString(hid_device, (VOID*)buffer, sizeof(buffer))
        || wcscmp(element->manufacturer, buffer) != 0))
    {
        return FALSE;
    }

    if (element->flags & XHID_ATTR_SERIALSTR &&
        (element->serial == NULL
        || !HidD_GetSerialNumberString(hid_device, (VOID*)buffer, sizeof(buffer))
        || wcscmp(element->serial, buffer) != 0))
    {
        return FALSE;
    }

    return TRUE;
}

static BOOL IsMatch_COMPOSITE(HANDLE hid_device, const XHID_COMPOSITE_MATCH * element)
{
    int i;
    BOOL result;

    if (element->items == NULL)
    {
        return TRUE;
    }

    switch (element->op)
    {
        case XHID_OP_AND:
        case XHID_OP_NAND:
            result = TRUE;
            for (i = 0; i <= element->last_idx; i++)
            {
                if (!IsMatch_Item(hid_device, element->type, i, element->items))
                {
                    result = FALSE;
                    break;
                }
            }
            break;
        case XHID_OP_OR:
        case XHID_OP_NOR:
            result = FALSE;
            for (i = 0; i <= element->last_idx; i++)
            {
                if (IsMatch_Item(hid_device, element->type, i, element->items))
                {
                    result = TRUE;
                    break;
                }
            }
            break;
        default:
            WARN("unknown op %d\n", element->op);
            return FALSE;
    }
    switch (element->op)
    {
        case XHID_OP_AND:
        case XHID_OP_OR:
            return result;
        default:
            return !result;
    }
}

static BOOL IsMatch_Item(HANDLE hid_device, XHID_MATCH_TYPE type, int index, const void * items)
{
    switch (type)
    {
        case XHID_MATCHTYPE_COMPOSITE:
            return IsMatch_COMPOSITE(hid_device, ((const XHID_COMPOSITE_MATCH *)items) + index);
        case XHID_MATCHTYPE_BASEATTR:
            return IsMatch_BASEATTR(hid_device, ((const XHID_BASEATTR_MATCH *)items) + index);
        default:
            WARN("unknown type %d\n", type);
            return FALSE;
    }
}


BOOL xhid_IsMatch(HANDLE hid_device, const XHID_MAPPING * mapping)
{
    return IsMatch_COMPOSITE(hid_device, &(mapping->match));
}
