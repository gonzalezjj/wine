#include "config.h"

#define _GNU_SOURCE

#include "xinput_backend_evdev.h"

#undef XINPUTW_BACKEND_EVDEV_ENABLED
#undef XINPUTW_BACKEND_EVDEV_THREADING_ENABLED

#if defined(HAVE_SYS_EVENTFD_H) \
    && defined(HAVE_SYS_SELECT_H)
#define XINPUTW_BACKEND_EVDEV_THREADING_ENABLED
#else
#warning "No threading support for the evdev xinput backend"
#endif

/* Check if the system meets all the requirements to enable this backend */
#if (defined(HAVE_LINUX_IOCTL_H) || defined(HAVE_SYS_IOCTL_H)) \
    && defined(HAVE_LINUX_INPUT_H) \
    && defined(HAVE_SYS_STAT_H) \
    && defined(HAVE_SYS_TYPES_H)
#define XINPUTW_BACKEND_EVDEV_ENABLED
#else
#warning "Evdev xinput backend disabled"
#endif

#ifdef XINPUTW_BACKEND_EVDEV_ENABLED

#include "wine/debug.h"

/* Required includes */
#ifdef HAVE_LINUX_INPUT_H
# include <linux/input.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#elif defined HAVE_LINUX_IOCTL_H
# include <linux/ioctl.h>
#endif

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif


/* Threading includes */
#ifdef HAVE_SYS_EVENTFD_H
# include <sys/eventfd.h>
#endif

#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "xinput.h"
#include "xinput_backend.h"
#include "xinput_util.h"


#define DIV_CEIL(a, b) (((a) + (b) - 1) / (b))
#define BITMAP_SIZE(s) (DIV_CEIL((s), (sizeof(BITMAP_T) * 8)))

#define DEV_INPUT_PATH "/dev/input"

WINE_DEFAULT_DEBUG_CHANNEL(xinputevdev);

typedef unsigned long BITMAP_T;

struct BUTTON_VALUE_MAP {
    __s32 value_released;
    __s32 value_pressed;
};

/* Maps an evdev device's buttons and axes to xinput buttons and axes */
struct CONTROLLER_INPUT_MAP {
    XINPUTW_EVENT_CODE xinput_code;
    XINPUTW_EVENT_MAP target_map;

    __s16 evdev_type;
    __s16 evdev_code;
    struct BUTTON_VALUE_MAP source_button_map;
};

struct CONTROLLER_MATCH {
    enum {
        CONTROLLER_MATCH_NAME = 0x01,
        CONTROLLER_MATCH_PATH = 0x02,
        CONTROLLER_MATCH_LOCATION = 0x04,
        CONTROLLER_MATCH_UID = 0x08,
        CONTROLLER_MATCH_BUSTYPE = 0x10,
        CONTROLLER_MATCH_VENDOR = 0x20,
        CONTROLLER_MATCH_PRODUCT = 0x40,
        CONTROLLER_MATCH_VERSION = 0x80
    } match_flags;

    char path[256];
    char name[256];
    char location[256];
    char uid[256];
    struct input_id id;
};

/* Access to all input mappings for a certain controller */
struct CONTROLLER_DEFINITION {
    const char name[256];
    const struct CONTROLLER_MATCH *matches;
    const SIZE_T matches_size;
    const struct CONTROLLER_INPUT_MAP *maps;
    const SIZE_T maps_size;
};

/* Contains read-only info about an evdev device */
struct WINE_EVDEV_INFO {
    char path[256];
    char name[256];
    char location[256];
    char uid[256];
    struct input_id id;
    BITMAP_T capabilities[BITMAP_SIZE(EV_CNT)];
    BITMAP_T keys[BITMAP_SIZE(KEY_CNT)];
    BITMAP_T axes[BITMAP_SIZE(ABS_CNT)];
    BITMAP_T ffbits[BITMAP_SIZE(FF_CNT)];
    struct input_absinfo axis_info[ABS_CNT];
};

/* Represents a game device slot */
struct EVDEV_SLOT {
    int fd;
    int event_fd;

    DWORD slot_index;

    struct WINE_EVDEV_INFO info;
    const struct CONTROLLER_DEFINITION *controller_definition;
    __s16 rumble_effect_id;

    HANDLE reader_thread;
};

static const struct CONTROLLER_INPUT_MAP default_xboxdrv_map_items[] = {
    { WINE_BTN_A, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_A, { 0, 0x7fff} },
    { WINE_BTN_B, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_B, { 0, 0x7fff} },
    { WINE_BTN_Y, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_Y, { 0, 0x7fff} },
    { WINE_BTN_X, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_X, { 0, 0x7fff} },
    { WINE_BTN_START, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_START, {0, 0x7fff} },
    { WINE_BTN_BACK, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_SELECT, {0, 0x7fff} },
    { WINE_BTN_LSHOULDER, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_TL, {0, 0x7fff} },
    { WINE_BTN_RSHOULDER, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_TR, {0, 0x7fff} },
    { WINE_BTN_LTHUMB, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_THUMBL, {0, 0x7fff} },
    { WINE_BTN_RTHUMB, { .button = VAL_TO_BTN_GT_ZERO }, EV_KEY, BTN_THUMBR, {0, 0x7fff} },
    { WINE_BTN_DPAD_UP, { .button = VAL_TO_BTN_LT_ZERO }, EV_ABS, ABS_HAT0Y, { 0, 0 } },
    { WINE_BTN_DPAD_DOWN, { .button = VAL_TO_BTN_GT_ZERO }, EV_ABS, ABS_HAT0Y, { 0, 0 } },
    { WINE_BTN_DPAD_LEFT, { .button = VAL_TO_BTN_LT_ZERO }, EV_ABS, ABS_HAT0X, { 0, 0 } },
    { WINE_BTN_DPAD_RIGHT, { .button = VAL_TO_BTN_GT_ZERO }, EV_ABS, ABS_HAT0X, { 0, 0 } },
    { WINE_AXIS_LTRIGGER, { .axis = AXIS_MAP_REGULAR }, EV_ABS, ABS_Z, { 0l, 0l } },
    { WINE_AXIS_RTRIGGER, { .axis = AXIS_MAP_REGULAR }, EV_ABS, ABS_RZ, { 0l, 0l } },
    { WINE_AXIS_LTHUMB_X, { .axis = AXIS_MAP_REGULAR }, EV_ABS, ABS_X, { 0l, 0l } },
    { WINE_AXIS_LTHUMB_Y, { .axis = AXIS_MAP_INVERTED }, EV_ABS, ABS_Y, { 0l, 0l } },
    { WINE_AXIS_RTHUMB_X, { .axis = AXIS_MAP_REGULAR }, EV_ABS, ABS_RX, { 0l, 0l } },
    { WINE_AXIS_RTHUMB_Y, { .axis = AXIS_MAP_INVERTED }, EV_ABS, ABS_RY, { 0l, 0l } }
};

static const struct CONTROLLER_MATCH default_xboxdrv_match[] = {
    { CONTROLLER_MATCH_NAME,
        .path = "" , .name = "Microsoft X-Box 360 pad", .location = "", .uid = "", .id = { .bustype = 0, .vendor = 0, . product = 0, .version = 0} }
};

const struct CONTROLLER_DEFINITION controller_definitions[] = {
    {
        "xpad",
        default_xboxdrv_match, sizeof(default_xboxdrv_match) / sizeof(default_xboxdrv_match[0]),
        default_xboxdrv_map_items, sizeof(default_xboxdrv_map_items) / sizeof(default_xboxdrv_map_items[0])
    }
};
const int controller_definitions_count = sizeof(controller_definitions) / sizeof(controller_definitions[0]);


static struct EVDEV_SLOT slots[XUSER_MAX_COUNT];


static BOOL get_bit(BITMAP_T *bitmap, unsigned int bit){
    unsigned int index = bit / (8 * sizeof(BITMAP_T));
    unsigned int shift = bit - (index * 8 * sizeof(BITMAP_T));
    return (bitmap[index] & (1 << shift)) >> shift;
}

void trace_get_bits(BITMAP_T *bits, int array_count) {
    char target[200];
    int i, j, k, idx;
    idx = 0;
    for (i = 0; i < array_count; ++i) {
        for (j = 0; j < sizeof(BITMAP_T); ++j) {
            for (k = j * 8; k < (j + 1) * 8; ++k) {
                target[idx++] = get_bit(bits, i * sizeof(BITMAP_T) + k) ? '1' : '0';
            }
            target[idx++] = ' ';
        }
        if(!(((i + 1) * sizeof(BITMAP_T)) % 16)) {
            target[idx] = 0;
            TRACE("     %s\n", target);
            idx = 0;
        }
    }
    if(((i * sizeof(BITMAP_T)) % 16)) {
        target[idx] = 0;
        TRACE("     %s\n", target);
        idx = 0;
    }
    target[idx] = 0;
}

static const struct CONTROLLER_DEFINITION * dev_try_find_match(const struct WINE_EVDEV_INFO *info) {
    int i, j;

    TRACE("dev path %s\n", info->path);
    for (i = 0; i < controller_definitions_count; ++i) {
        TRACE("config name \"%s\"\n", controller_definitions[i].name);
        for (j = 0; j < controller_definitions[i].matches_size; ++j) {
            TRACE("trying match definition %d\n", j);
            /* Check if the required parameters match */
            if (controller_definitions[i].matches[j].match_flags & CONTROLLER_MATCH_NAME
                && strcmp(controller_definitions[i].matches[j].name, info->name)) {
                TRACE("name mismatch (target \"%s\", dev: \"%s\")\n", controller_definitions[i].matches[j].name, info->name);
                continue;
            }
            if (controller_definitions[i].matches[j].match_flags & CONTROLLER_MATCH_PATH
                && strcmp(controller_definitions[i].matches[j].path, info->path)) {
                TRACE("path mismatch (target \"%s\", dev: \"%s\")\n", controller_definitions[i].matches[j].path, info->path);
                continue;
            }
            if (controller_definitions[i].matches[j].match_flags & CONTROLLER_MATCH_LOCATION
                && strcmp(controller_definitions[i].matches[j].location, info->location)) {
                TRACE("location mismatch (target \"%s\", dev: \"%s\")\n", controller_definitions[i].matches[j].location, info->location);
                continue;
            }
            if (controller_definitions[i].matches[j].match_flags & CONTROLLER_MATCH_UID
                && strcmp(controller_definitions[i].matches[j].uid, info->uid)) {
                TRACE("uid mismatch (target \"%s\", dev: \"%s\")\n", controller_definitions[i].matches[j].uid, info->uid);
                continue;
            }
            if (controller_definitions[i].matches[j].match_flags & CONTROLLER_MATCH_BUSTYPE
                && controller_definitions[i].matches[j].id.bustype != info->id.bustype) {
                TRACE("bustype mismatch (target \"%x\", dev: \"%x\")\n", controller_definitions[i].matches[j].id.bustype, info->id.bustype);
                continue;
            }
            if (controller_definitions[i].matches[j].match_flags & CONTROLLER_MATCH_VENDOR
                && controller_definitions[i].matches[j].id.vendor != info->id.vendor) {
                TRACE("vendor mismatch (target \"%x\", dev: \"%x\")\n", controller_definitions[i].matches[j].id.vendor, info->id.vendor);
                continue;
            }
            if (controller_definitions[i].matches[j].match_flags & CONTROLLER_MATCH_PRODUCT
                && controller_definitions[i].matches[j].id.product != info->id.product) {
                TRACE("product mismatch (target \"%x\", dev: \"%x\")\n", controller_definitions[i].matches[j].id.product, info->id.product);
                continue;
            }
            if (controller_definitions[i].matches[j].match_flags & CONTROLLER_MATCH_VERSION
                && controller_definitions[i].matches[j].id.version != info->id.version) {
                TRACE("version mismatch (target \"%x\", dev: \"%x\")\n", controller_definitions[i].matches[j].id.version, info->id.version);
                continue;
            }

            TRACE("config \"%s\" valid for device %s\n", controller_definitions[i].name, info->path);
            /* There was no mismatch, return this definition */
            return &controller_definitions[i];
        }
    }
    TRACE("no config found for dev %s\n", info->path);
    return NULL;
}

static int dev_get_axis_info(int fd, int axis, struct input_absinfo *info) {
    int rc;
    TRACE("axis %u\n", axis);
    if(ioctl(fd, EVIOCGABS(axis), info) < 0) {
        rc = -errno;
        TRACE("error getting axis %u info: %d\n", axis, rc);
        return rc;
    }
    return 0;
}

/*
 * Read a device's evdev info
 * Returns 0 on success.
 */
static int dev_read_info(int fd, struct WINE_EVDEV_INFO *info) {
    int rc;
    int i;

    TRACE("device %s info:\n", info->path);

    memset(info->name, 0, sizeof(info->name));
    if (ioctl(fd, EVIOCGNAME(sizeof(info->name) - 1), info->name) < 0)
        if(errno != ENOENT)
            return -errno;
    TRACE("  -> name: %s\n", info->name);

    memset(info->location, 0, sizeof(info->location));
    if (ioctl(fd, EVIOCGPHYS(sizeof(info->location) - 1), info->location) < 0)
        if(errno != ENOENT)
            return -errno;
    TRACE("  -> loc: %s\n", info->location);

    memset(info->uid, 0, sizeof(info->uid));
    if (ioctl(fd, EVIOCGNAME(sizeof(info->uid) - 1), info->uid) < 0)
        if(errno != ENOENT)
            return -errno;
    TRACE("  -> uid: %s\n", info->uid);

    if (ioctl(fd, EVIOCGID, &info->id) < 0)
        return -errno;
    TRACE("  -> id: bustype %hx, vendor %hx, product %hx, version %hx\n", info->id.bustype, info->id.vendor, info->id.product, info->id.version);

    if (ioctl(fd, EVIOCGBIT(0, sizeof(info->capabilities)), info->capabilities) < 0)
        return -errno;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(info->keys)), info->keys) < 0)
        return -errno;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(info->axes)), info->axes) < 0)
        return -errno;
    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(info->ffbits)), info->ffbits) < 0)
        return -errno;

    if (TRACE_ON(xinputevdev)) {
        TRACE("  -> ev bits:\n");
        trace_get_bits(info->capabilities, sizeof(info->capabilities) / sizeof(BITMAP_T));
        TRACE("  -> key bits:\n");
        trace_get_bits(info->keys, sizeof(info->keys) / sizeof(BITMAP_T));
        TRACE("  -> axis bits:\n");
        trace_get_bits(info->axes, sizeof(info->axes) / sizeof(BITMAP_T));
        TRACE("  -> ff bits:\n");
        trace_get_bits(info->ffbits, sizeof(info->ffbits) / sizeof(BITMAP_T));
    }

    for (i = 0; i < ABS_MAX; ++i){
        if (!get_bit(info->axes, i)) {
            memset((void *)(info->axis_info + i), 0, sizeof(struct input_absinfo));
        } else {
            if ((rc = dev_get_axis_info(fd, i, info->axis_info + i)) < 0)
                return rc;
            TRACE("  -> axis %d: min: %d; max: %d\n", i, info->axis_info[i].minimum, info->axis_info[i].maximum);
        }
    }

    return 0;
}


static void slot_parse_event(DWORD slot, const struct input_event *event) {
    int i;
    XINPUTW_EVENT xinput_event;

    TRACE("slot %d, event type %d, code %d, value %d\n", slot, event->type, event->code, event->value);

    /* FIXME: This ignores event->time */
    xinput_event.timestamp = GetTickCount64();

    for (i = 0; i < slots[slot].controller_definition->maps_size; ++i) {
        if (event->type == slots[slot].controller_definition->maps[i].evdev_type
            && event->code == slots[slot].controller_definition->maps[i].evdev_code
            && slots[slot].controller_definition->maps[i].xinput_code < WINE_CONTROL_COUNT) {

            xinput_event.code = slots[slot].controller_definition->maps[i].xinput_code;
            memcpy((void *)&xinput_event.value_map, &slots[slot].controller_definition->maps[i].target_map, sizeof(xinput_event.value_map));

            if (event->type == EV_KEY) {
                xinput_event.value = event->value
                    ? slots[slot].controller_definition->maps[i].source_button_map.value_pressed
                    : slots[slot].controller_definition->maps[i].source_button_map.value_released;
            } else if (event->type == EV_ABS) {
                xinput_event.value = xiw_util_ConvToXIWValue(event->value, slots[slot].info.axis_info[event->code].minimum, slots[slot].info.axis_info[event->code].maximum);
            } else {
                WARN("invalid evdev event type %d\n", event->type);
                return;
            }

            xiw_core_PushEvent(slot, &xinput_event);
        }
    }
}


static int slot_sync_state(DWORD slot) {
    uint32_t i;
    int rc;
    BITMAP_T key_state[BITMAP_SIZE(KEY_CNT)];
    struct input_absinfo abs_info;
    struct input_event event;

    gettimeofday(&event.time, NULL);

    TRACE("slot %hu\n", slot);

    /* Buttons */
    if (ioctl(slots[slot].fd, EVIOCGKEY(sizeof(key_state)), &key_state) < 0) {
        rc = -errno;
        TRACE("slot %hu - getting key state failed. error code: %d\n", slot, rc);
        return rc;
    }
    event.type = EV_KEY;
    for (i = 0; i < KEY_CNT; ++i) {
        if (!get_bit(slots[slot].info.keys, i)) continue;
        event.code = i;
        event.value = get_bit(key_state, i);
        slot_parse_event(slot, &event);
    }

    event.type = EV_ABS;
    for (i = 0; i < ABS_CNT; ++i) {
        if (!get_bit(slots[slot].info.axes, i)) continue;
        if ((rc = dev_get_axis_info(slots[slot].fd, i, &abs_info)) < 0)
            return rc;
        event.code = i;
        event.value = abs_info.value;
        slot_parse_event(slot, &event);
    }

    return 0;
}

static int slot_update_state(unsigned int slot) {
    int rc;
    struct input_event event;

    TRACE("slot %hu\n", slot);

    while ((rc = read(slots[slot].fd, &event, sizeof(event))) > 0) {
        switch (event.type) {
            case EV_SYN:
                if (event.code == SYN_DROPPED) {
                    return slot_sync_state(slot);
                }
            case EV_KEY:
            case EV_ABS:
                slot_parse_event(slot, &event);
                break;
        }
    }

    /* Anything except EAGAIN (ie. "no more data right now") is an error */
    if (rc < 0 && errno != EAGAIN)
        return -errno;

    return 0;
}


static int scan_evdev_filter(const struct dirent * entry) {
    return !memcmp(entry->d_name, "event", 5);
}

static void fill_capabilities(struct EVDEV_SLOT *slot, XINPUTW_DEV_CAPABILITIES *capabilities) {
    WORD i;
    const struct input_absinfo *axis_info;
    BOOL has_button;

    memset((void *)capabilities, 0, sizeof(*capabilities));

    for (i = 0; i < slot->controller_definition->maps_size; ++i) {
        if (slot->controller_definition->maps[i].xinput_code >= WINE_BTN_MIN && slot->controller_definition->maps[i].xinput_code <= WINE_BTN_MAX) {
            /* Wine XInput button */
            has_button = FALSE;

            if (slot->controller_definition->maps[i].evdev_type == EV_KEY)
                has_button = get_bit(slot->info.keys, slot->controller_definition->maps[i].evdev_code);
            else if (slot->controller_definition->maps[i].evdev_type == EV_ABS)
                has_button = get_bit(slot->info.axes, slot->controller_definition->maps[i].evdev_code);

            if (has_button)
                xiw_util_SetCapabilitiesBtn(&capabilities->buttons, slot->controller_definition->maps[i].xinput_code, TRUE);
        } else if (slot->controller_definition->maps[i].xinput_code >= WINE_AXIS_MIN && slot->controller_definition->maps[i].xinput_code <= WINE_AXIS_MAX) {
            /* Wine XInput axis */

            if (slot->controller_definition->maps[i].evdev_type == EV_KEY) {
                if (get_bit(slot->info.keys, slot->controller_definition->maps[i].evdev_code)) {
                    xiw_util_SetCapabilitiesAxis(capabilities->axes, slot->controller_definition->maps[i].xinput_code, 0, 1);
                }
            } else if (slot->controller_definition->maps[i].evdev_type == EV_ABS) {
                if (get_bit(slot->info.axes, slot->controller_definition->maps[i].evdev_code)) {
                    axis_info = &slot->info.axis_info[slot->controller_definition->maps[i].evdev_code];
                    xiw_util_SetCapabilitiesAxis(capabilities->axes, slot->controller_definition->maps[i].xinput_code, axis_info->minimum, axis_info->maximum);
                }
            }
        }
    }

    if (get_bit(slot->info.capabilities, EV_FF))
        capabilities->has_rumble = TRUE;
}

#ifdef XINPUTW_BACKEND_EVDEV_THREADING_ENABLED

static DWORD WINAPI reader_thread_main(LPVOID args) {
    struct EVDEV_SLOT *slot;
    int ndfs;
    fd_set fileset;
    int64_t event_val;

    slot = (struct EVDEV_SLOT *)args;
    if (slot->fd < 0 || slot->event_fd < 0)
        return 0;

    ndfs = 1 + (slot->fd > slot->event_fd ? slot->fd : slot->event_fd);
    FD_ZERO(&fileset);
    FD_SET(slot->fd, &fileset);
    FD_SET(slot->event_fd, &fileset);

    while (select(ndfs, &fileset, NULL, NULL, NULL) >= 0) {
        TRACE("new data available\n");
        if (read(slot->event_fd, &event_val, sizeof(event_val)) < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                WARN("error reading event_fd for slot %d\n", slot->slot_index);
                break;
            }
        } else {
            if (event_val > 0) break;
        }

        if (slot_update_state(slot->slot_index) != 0)
            break;
    }

    close(slot->fd);
    slot->fd = -1;
    close(slot->event_fd);
    slot->event_fd = -1;

    return 0;
}

#endif /* XINPUTW_BACKEND_EVDEV_THREADING_ENABLED */





/* Interface methods */

const XINPUTW_BACKEND xinput_backend_evdev;

/*
 * Initialize the backendoutines. Is called exactly once by xinput_core
 * during initialization, before calling anything else in this backend.
 */
void evdev_backend_initialize(void) {
    unsigned int i;
    TRACE("initializing\n");
    for (i = 0; i < XUSER_MAX_COUNT; i++) {
        slots[i].fd = -1;
        slots[i].event_fd = -1;
        slots[i].slot_index = i;
    }
}



/*
 * Try to connect a new device to the given slot.
 * Returns: TRUE if successful, FALSE if not (ie. if no new device is available)
 */
BOOL evdev_backend_TryConnectDevice(DWORD target_slot_index, XINPUTW_DEV_CAPABILITIES *capabilities) {
    int fd;
    int rc;

    int i;
    BOOL dev_is_already_open;

    int dev_count, dev_idx;
    struct dirent **dir_entries;
    struct dirent *dir_entry;

    char dev_root_path[512], dev_path[512];
    struct EVDEV_SLOT *slot;
    struct WINE_EVDEV_INFO dev_info;
    const struct CONTROLLER_DEFINITION *matching_definition;

    TRACE("slot %d\n", target_slot_index);

    if (slots[target_slot_index].fd >= 0) return TRUE;

    strcpy(dev_root_path, DEV_INPUT_PATH);
    strcat(dev_root_path, "/");

    if ((dev_count = scandir(DEV_INPUT_PATH, &dir_entries, scan_evdev_filter, versionsort)) >= 0) {
        dev_idx = 0;

        /* Scan every /dev/input/event* device and return the first match */
        while (dev_idx < dev_count) {
            dir_entry = dir_entries[dev_idx++];
            strcpy(dev_path, dev_root_path);
            strcat(dev_path, dir_entry->d_name);

            dev_is_already_open = FALSE;
            for (i = 0; i < XUSER_MAX_COUNT; ++i) {
                if (slots[i].fd >= 0 && strcmp(slots[i].info.path, dev_path) == 0) {
                    dev_is_already_open = TRUE;
                    break;
                }
            }
            if (dev_is_already_open)
                continue;

            /* Open the device */
            fd = open(dev_path, O_RDWR | O_NONBLOCK);

            strcpy(dev_info.path, dev_path);
            if ((rc = dev_read_info(fd, &dev_info)) < 0) {
                TRACE("capabilities of %s could not be read. error code: %d\n", dev_path, rc);
                continue;
            }

            TRACE("opened evdev %s with name %s\n", dev_info.path, dev_info.name);

            /* Check whether this is an xinput device and close it if not */
            if ((matching_definition = dev_try_find_match(&dev_info)) == NULL) {
                close(fd);
                TRACE("%s is not an xinput device\n", dev_path);
            } else {
                slot = &slots[target_slot_index];
                slot->fd = fd;

                /* Initialize slot values */
                /* TODO: different maps */
                slot->controller_definition = matching_definition;

                memcpy((void *)&slot->info, (void *)&dev_info, sizeof(dev_info));
                slot->rumble_effect_id = -1;

                TRACE("slot %u connected: %s (%s)\n", target_slot_index, dev_info.path, dev_info.name);

                break;
            }
        }
        free(dir_entries);
    } else {
        WARN("could not read dir %s\n", DEV_INPUT_PATH);
    }

    if (slots[target_slot_index].fd >= 0) {
        fill_capabilities(&slots[target_slot_index], capabilities);

        if ((slots[target_slot_index].event_fd = eventfd(0, EFD_NONBLOCK)) < 0) {
            WARN("Could not create eventfd for slot %d\n", target_slot_index);
            close(slots[target_slot_index].fd);
            slots[target_slot_index].fd = -1;
            return FALSE;
        }

        slot_sync_state(target_slot_index);

#ifdef XINPUTW_BACKEND_EVDEV_THREADING_ENABLED
        slots[target_slot_index].reader_thread =
            CreateThread(NULL, 0, reader_thread_main, &slots[target_slot_index], 0, NULL);
#endif /* XINPUTW_BACKEND_EVDEV_THREADING_ENABLED */

        return TRUE;
    }

    return FALSE;
}

/*
 * Close the device at the given slot. Called if access to the device fails, or when shutting down xinput
 */
void evdev_backend_DisconnectDevice(DWORD slot_index) {
#ifdef XINPUTW_BACKEND_EVDEV_THREADING_ENABLED
    const int64_t value = 1;
#endif

    TRACE("slot %d\n", slot_index);

#ifdef XINPUTW_BACKEND_EVDEV_THREADING_ENABLED
    if (slots[slot_index].event_fd >= 0) {
        write(slots[slot_index].event_fd, &value, 8);
        WaitForSingleObject(slots[slot_index].reader_thread, 0);
    }
#else
    close(slots[slot_index].fd);
    slots[slot_index].fd = -1;
#endif /* XINPUTW_BACKEND_EVDEV_THREADING_ENABLED */
}

/*
 * Synchronize the gamepad state for a given slot.
 * Returns: TRUE if successful (even for no-ops), FALSE if the device was disconnected
 */
BOOL evdev_backend_SyncKeyState(DWORD slot_index) {
    TRACE("slot %d\n", slot_index);
    if (slots[slot_index].fd < 0)
        return FALSE;

#ifdef XINPUTW_BACKEND_EVDEV_THREADING_ENABLED
    /* On failure, the thread will close the file descriptor. We made it this far, so there were no errors */
    return TRUE;
#else
    /* Synchronously update the state */
    return slot_update_state(slot_index) == 0;
#endif /* XINPUTW_BACKEND_EVDEV_THREADING_ENABLED */
}

/*
 * Synchronize the gamepad battery for a given slot.
 * slot_index: The index of the slot.
 * battery_level (out): The current battery level. If the battery state is unknown, battery_level should be set to a value less than 0 (eg. -1)
 * Returns: TRUE if successful (even for no-ops), FALSE if the device was disconnected
 */
BOOL evdev_backend_SyncBatteryState(DWORD slot_index, XINPUTW_VALUE *battery_level) {
    TRACE("slot %d\n", slot_index);
    *battery_level = -1;    /* Not supported */

    return slots[slot_index].fd >= 0;
}

/*
 * Synchronize the gamepad battery for a given slot.
 * slot_index: The index of the slot.
 * rumble: The rumble values to be set. The effect should continue until SetRumble is called again
 * Returns: TRUE if successful (even for no-ops), FALSE if the device was disconnected
 */
BOOL evdev_backend_SetRumble(DWORD slot_index, const XINPUTW_DEV_RUMBLE *rumble) {
    struct ff_effect effect;
    struct input_event event;

    TRACE("slot %d, values (%d, %d)\n", slot_index, rumble->hf, rumble->lf);
    if (slots[slot_index].fd < 0) return FALSE;

    event.type = EV_FF;

    /* Check if we have to play an effect or stop */
    if(rumble->hf || rumble->lf) {
        effect.type = FF_RUMBLE;
        effect.id = slots[slot_index].rumble_effect_id;
        effect.direction = 0;
        memset((void *)&effect.trigger, 0, sizeof(effect.trigger));
        effect.replay.length = 0xfffful;
        effect.replay.delay = 0;
        effect.u.rumble.strong_magnitude = rumble->hf;
        effect.u.rumble.weak_magnitude = rumble->lf;

        if (ioctl(slots[slot_index].fd, EVIOCSFF, &effect) < 0) {
            TRACE("upload rumble effect failed. errno: %d\n", errno);
            return FALSE;
        }

        slots[slot_index].rumble_effect_id = effect.id;

        /* Play indefinitely */
        event.value = 0x7fffffff;
    } else {
        if (slots[slot_index].rumble_effect_id == -1)
            return TRUE;

        event.value = 0;
    }
    event.code = slots[slot_index].rumble_effect_id;
    if (write(slots[slot_index].fd, (const void *) &event, sizeof(event)) < 0) {
        TRACE("play rumble effect failed. errno: %d\n", errno);
        return FALSE;
    }

    return TRUE;
}

/* Interface export */
const XINPUTW_BACKEND xinput_backend_evdev = {
    .Name = "Wine XInput linux evdev backend",
    .Initialize = &evdev_backend_initialize,
    .TryConnectDevice = &evdev_backend_TryConnectDevice,
    .DisconnectDevice = &evdev_backend_DisconnectDevice,
    .SyncKeyState = &evdev_backend_SyncKeyState,
    .SyncBatteryState = &evdev_backend_SyncBatteryState,
    .SetRumble = &evdev_backend_SetRumble
};

#else /* XINPUTW_BACKEND_EVDEV_ENABLED */

/* Interface export */
const XINPUTW_BACKEND xinput_backend_evdev = {
    .Name = "Wine XInput linux evdev backend",
    .Initialize = NULL,
    .TryConnectDevice = NULL,
    .DisconnectDevice = NULL,
    .SyncKeyState = NULL,
    .SyncBatteryState = NULL,
    .SetRumble = NULL
};

#endif /* XINPUTW_BACKEND_EVDEV_ENABLED */
