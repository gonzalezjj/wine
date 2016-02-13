/*
 * The Wine project - XInput Joystick Library - VirtualKey queue management
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

#include "wine/debug.h"
#include "xinput_core.h"

static const int KEYSTROKE_QUEUE_DISCARD_MS = 500;

static const XINPUTW_KEYSTROKE * KeystrokeQueueGetFrontInternal(XINPUTW_KEYSTROKE_QUEUE * queue) {
    if (queue->head == queue->tail) return NULL;
    return queue->elements + queue->head;
}

static void KeystrokeQueueDiscardOldEntries(XINPUTW_KEYSTROKE_QUEUE * queue) {
    ULONGLONG timestamp;
    const XINPUTW_KEYSTROKE * front;

    timestamp = GetTickCount64();

    while ((front = KeystrokeQueueGetFrontInternal(queue))
        && front->timestamp < timestamp - KEYSTROKE_QUEUE_DISCARD_MS) {
        xiw_vk_KeystrokeQueuePop(queue);
    }
}

const XINPUTW_KEYSTROKE * xiw_vk_KeystrokeQueueGetFront(XINPUTW_KEYSTROKE_QUEUE * queue) {
    KeystrokeQueueDiscardOldEntries(queue);
    return KeystrokeQueueGetFrontInternal(queue);
}

void xiw_vk_KeystrokeQueuePop(XINPUTW_KEYSTROKE_QUEUE * queue) {
    if (queue->head == queue->tail)
        return;
    if (++(queue->head) >= KEYSTROKE_QUEUE_SIZE)
        queue->head = 0;
}

void xiw_vk_KeystrokeQueuePush(XINPUTW_KEYSTROKE_QUEUE * queue, const XINPUTW_KEYSTROKE * element) {
    if (queue->tail + 1 == queue->head || (queue->tail + 1 >= KEYSTROKE_QUEUE_SIZE && queue->head == 0)) {
        /* No more space in the queue, discard the oldest (first) element */
        xiw_vk_KeystrokeQueuePop(queue);
    }
    memcpy((void *)(queue->elements + queue->tail), (void *)element, sizeof(XINPUTW_KEYSTROKE));
    if(++(queue->tail) >= KEYSTROKE_QUEUE_SIZE)
        queue->tail = 0;
}

void xiw_vk_KeystrokeQueueClear(XINPUTW_KEYSTROKE_QUEUE * queue) {
    queue->head = 0;
    queue->tail = 0;
}
