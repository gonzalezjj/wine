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

#ifndef __WINE_DLLS_XINPUT_CORE_H
#define __WINE_DLLS_XINPUT_CORE_H

#include "xinput_util.h"

#define KEYSTROKE_QUEUE_SIZE 1024

/* Stores a keystroke with a timestamp */
typedef struct _XINPUTW_KEYSTROKE {
    ULONGLONG timestamp;
    XINPUT_KEYSTROKE keystroke;
} XINPUTW_KEYSTROKE;

/* Stores several keystrokes in a queue */
typedef struct _XINPUTW_KEYSTROKE_QUEUE {
    XINPUTW_KEYSTROKE elements[KEYSTROKE_QUEUE_SIZE];
    /* The first valid element */
    unsigned int head;
    /* One element after the last valid one, ie. the first free element */
    unsigned int tail;
} XINPUTW_KEYSTROKE_QUEUE;


/* Defines the current state of a control. Used to emit VirtualKey events */
typedef enum _XINPUTW_VK_AREA {
    /* Deadzone / Released */
    VK_AREA_NONE,
    /* Triggers / Buttons */
    VK_AREA_PRESSED,
    /* Thumbpads */
    VK_AREA_L,
    VK_AREA_LD,
    VK_AREA_D,
    VK_AREA_RD,
    VK_AREA_R,
    VK_AREA_RU,
    VK_AREA_U,
    VK_AREA_LU
} XINPUTW_VK_AREA;

/* Used to track the state (pressed/not) of a single control to emit VirtualKey events as needed */
typedef struct _XINPUTW_VK_STATE {
    /* Currently pressed VirtualKey area */
    XINPUTW_VK_AREA area;
    /* Timestamp of last sent Keystroke */
    ULONGLONG timestamp;
    BOOL isRepeat;
} XINPUTW_VK_STATE;

/*
 * Control names. Each thumbpad is a single entry, as opposed to XINPUTW_EVENT_CODE,
 * where each thumbpad has an entry per axis
 */
typedef enum _XINPUTW_VK_CONTROLNAME {
    WVK_BTN_A = 0ul,
    WVK_BTN_B,
    WVK_BTN_Y,
    WVK_BTN_X,
    WVK_BTN_START,
    WVK_BTN_BACK,
    WVK_BTN_LSHOULDER,
    WVK_BTN_RSHOULDER,
    WVK_BTN_LTHUMB,
    WVK_BTN_RTHUMB,
    WVK_BTN_DPAD_UP,
    WVK_BTN_DPAD_DOWN,
    WVK_BTN_DPAD_LEFT,
    WVK_BTN_DPAD_RIGHT,
    WVK_AXIS_LTRIGGER,
    WVK_AXIS_RTRIGGER,
    WVK_AXIS_LTHUMB,
    WVK_AXIS_RTHUMB,
} XINPUTW_VK_CONTROLNAME;

/* Contains all the VK states of a gamepad */
typedef struct _XINPUTW_VK_STATES {
    XINPUTW_VK_STATE items[WVK_AXIS_RTHUMB + 1];
    BOOL lThumbIsSquare;
    BOOL rThumbIsSquare;
} XINPUTW_VK_STATES;

/*
 * Adds a new VirtualKey event to be returned by XInputGetKeystroke
 *
 * queue: A pointer to the XINPUTW_KEYSTROKE_QUEUE the event should be pushed into
 * element: A pointer to the new event. The event will be copied, allowing the caller to modify the
 *  data afterwards without altering the event in the queue
 */
void xiw_vk_KeystrokeQueuePush(XINPUTW_KEYSTROKE_QUEUE * queue, const XINPUTW_KEYSTROKE * element);

/*
 * Discards the front element from the VirtualKey event queue
 *
 * queue: A pointer to the XINPUTW_KEYSTROKE_QUEUE whose first element should be discarded
 */
void xiw_vk_KeystrokeQueuePop(XINPUTW_KEYSTROKE_QUEUE * queue);

/*
 * Returns the front element from the VirtualKey event queue. That event is kept in the queue until
 * xiw_vk_KeystrokeQueuePop is called
 *
 * queue: A pointer to the XINPUTW_KEYSTROKE_QUEUE the event should be retrieved from
 *
 * Returns: The first element from the queue, or NULL if the queue is empty. If any other
 *  operation is performed on the queue, data integrity cannot be guaranteed
 */
const XINPUTW_KEYSTROKE * xiw_vk_KeystrokeQueueGetFront(XINPUTW_KEYSTROKE_QUEUE * queue);

/*
 * Clears the VirtualKey event queue
 *
 * queue: A pointer to the XINPUTW_KEYSTROKE_QUEUE to be cleared
 */
void xiw_vk_KeystrokeQueueClear(XINPUTW_KEYSTROKE_QUEUE * queue);

/*
 * Notifies the VirtualKey code of an changed value to push VirtualKey events if necessary
 *
 * slot_index: The index of the slot/controller where the change happened
 * timestamp: The timestamp of the event as returned by GetTickCount64()
 * code: The XInput Wine event code, ie. the source of the changed value
 * state: The updated state of the controller
 * vkStates: The current VirtualKey states of the controller
 * keystrokeQueue: The VirtualKey keystroke queue of the controller
 */
void xiw_vk_Update(DWORD slot_index, ULONGLONG timestamp, XINPUTW_EVENT_CODE code,
                   const XINPUT_STATE * state, XINPUTW_VK_STATES * vkStates,
                   XINPUTW_KEYSTROKE_QUEUE * keystrokeQueue);

/*
 * Emits REPEAT VirtualKey events if necessary. Gets called on every call to XInputGetKeystroke
 *
 * slot_index: The index of the slot/controller where the change happened
 * vkStates: The current VirtualKey states of the controller
 * keystrokeQueue: The VirtualKey keystroke queue of the controller
 */
void xiw_vk_Repeat(DWORD slot_index, XINPUTW_VK_STATES *vkStates, XINPUTW_KEYSTROKE_QUEUE * keystrokeQueue);

#endif /* __WINE_DLLS_XINPUT_CORE_H */
