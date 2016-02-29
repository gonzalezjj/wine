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

#ifndef __WINE_DLLS_XINPUT_HID_MAPPING_H
#define __WINE_DLLS_XINPUT_HID_MAPPING_H

/* Enums are explicitly numbered to ensure correct serialization/deserialization */


typedef enum _XHID_BASEATTR
{
    XHID_ATTR_PRODSTR = 0x01l,
    XHID_ATTR_MANUFSTR = 0x02,
    XHID_ATTR_SERIALSTR = 0x04,
    XHID_ATTR_VID = 0x08,
    XHID_ATTR_PID = 0x10,
    XHID_ATTR_VERSION = 0x20
} XHID_BASEATTR;


/*
 * Matches base HID attributes, i.e. attributes of the top-level collection.
 *
 * If multiple attributes are checked, the result is combined with a logical AND.
 * Unused attribute fields will be ignored, so their value can be left unset.
 */
typedef struct _XHID_BASEATTR_MATCH
{
    XHID_BASEATTR flags;   /* Defines which attributes to check */

    WCHAR * product;
    WCHAR * manufacturer;
    WCHAR * serial;
    USHORT vid;
    USHORT pid;
    USHORT version;
} XHID_BASEATTR_MATCH;

/* Defines the type of the items in a composite match */
typedef enum _XHID_MATCH_TYPE
{
    XHID_MATCHTYPE_COMPOSITE = 0l,  /* Check and combine a group of matches */
    XHID_MATCHTYPE_BASEATTR = 1,    /* Match base HID attributes */
} XHID_MATCH_TYPE;

/* The operation to be performed when combining several match items in a composite match */
typedef enum _XHID_MATCH_OP
{
    XHID_OP_AND = 0l,
    XHID_OP_NAND = 1,
    XHID_OP_OR = 2,
    XHID_OP_NOR = 3,
} XHID_MATCH_OP;

/* Defines a composite match, where each item will be queried and the results combined */
typedef struct _XHID_COMPOSITE_MATCH
{
    /*
     * The operation to be performed to combine the items' result
     *
     * If omitted when using designated initializers in static mappings, it defaults to XHID_OP_AND
     */
    XHID_MATCH_OP op;

    /* The type of the items */
    XHID_MATCH_TYPE type;

    /*
     * The last item index, i.e. the count - 1
     *
     * Using this instead of a count property allows one to omit setting this property when
     * using designated initializers in static mappings if only one item is added
     */
    int last_idx;

    /*
     * A pointer to an array of match items of the specified type, or NULL to designate a
     * "catch-all" match that will always be positive.
     */
    void * items;
} XHID_COMPOSITE_MATCH;

typedef struct _XHID_MAPPING_ITEM
{
    /* TODO */
} XHID_MAPPING_ITEM;

typedef struct _XHID_MAPPING
{
    char name[256];

    /* The match for which this mapping applies */
    XHID_COMPOSITE_MATCH match;

    int count;
    XHID_MAPPING_ITEM * items;
} XHID_MAPPING;



#endif    /* __WINE_DLLS_XINPUT_HID_MAPPING_H */
