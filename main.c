#include <assert.h>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <libinput.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <uv.h>

#define DRAGMAN_VENDOR_ID 0xd01c
#define DRAGMAN_PRODUCT_ID 0x1c44

struct state {
    uv_loop_t loop;
    struct udev *udev;
    struct libinput *li;
    uv_poll_t li_watcher, touchpad_watcher;
    struct libevdev *touchpad_evdev;
    struct libevdev_uinput *virtual_pointer;
    uint64_t first_press_time, second_press_time;
    bool dragging;
};

static int open_restricted(const char *path, int flags, void *data) {
    int fd = open(path, flags);
    if (fd < 0)
        return -errno;

    return fd;
}

static void close_restricted(int fd, void *data) { close(fd); }

static const struct libinput_interface li_interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static void handle_li(uv_poll_t *watcher, int status, int events) {
    struct state *state = watcher->data;

    if (libinput_dispatch(state->li) < 0)
        return;

    struct libinput_event *ev;
    struct libinput_device *device;
    while ((ev = libinput_get_event(state->li))) {
        if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED) {
            device = libinput_event_get_device(ev);
            if (libinput_device_config_tap_get_finger_count(device)) {
                goto found_touchpad;
            }
        } else {
            goto unregister_watcher;
        }
    }
    return;

found_touchpad:;
    char dev_path[256];
    snprintf(dev_path, sizeof(dev_path), "/dev/input/%s",
             libinput_device_get_sysname(device));
    int fd = open(dev_path, O_RDONLY);
    libevdev_new_from_fd(fd, &state->touchpad_evdev);
unregister_watcher:
    uv_poll_stop(&state->li_watcher);
}

static struct libevdev_uinput *create_virtual_pointer(void) {
    struct libevdev *dev = libevdev_new();
    libevdev_set_name(dev, "Dragman virtual pointer");
    libevdev_set_id_vendor(dev, DRAGMAN_VENDOR_ID);
    libevdev_set_id_product(dev, DRAGMAN_PRODUCT_ID);
    libevdev_enable_event_type(dev, EV_KEY);
    libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, NULL);
    libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, NULL);
    libevdev_enable_event_code(dev, EV_KEY, BTN_MIDDLE, NULL);
    libevdev_enable_event_code(dev, EV_KEY, BTN_EXTRA, NULL);
    libevdev_enable_event_code(dev, EV_KEY, BTN_SIDE, NULL);
    libevdev_enable_event_type(dev, EV_REL);
    libevdev_enable_event_code(dev, EV_REL, REL_X, NULL);
    libevdev_enable_event_code(dev, EV_REL, REL_Y, NULL);
    libevdev_enable_event_code(dev, EV_REL, REL_WHEEL, NULL);

    struct libevdev_uinput *virtual_pointer = NULL;
    libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED,
                                       &virtual_pointer);
    libevdev_free(dev);

    return virtual_pointer;
}

static void virtual_pointer_send(struct libevdev_uinput *virtual_pointer,
                                 int state) {
    libevdev_uinput_write_event(virtual_pointer, EV_KEY, BTN_LEFT, state);
    libevdev_uinput_write_event(virtual_pointer, EV_SYN, SYN_REPORT, 0);
}

static uint64_t get_ms(void) { return uv_hrtime() / 1000000; }

static void handle_touchpad(uv_poll_t *watcher, int status, int events) {
    struct state *state = watcher->data;
    struct input_event ev;
    while (libevdev_next_event(state->touchpad_evdev, LIBEVDEV_READ_FLAG_NORMAL,
                               &ev) == LIBEVDEV_READ_STATUS_SUCCESS) {
        if (ev.type != EV_ABS || ev.code != ABS_MT_TRACKING_ID) {
            continue;
        }
        int slot = libevdev_get_current_slot(state->touchpad_evdev);
        if (ev.value != -1) {
            // pressed
            if (slot == 0) {
                // first finger
                state->first_press_time = get_ms();
            } else if (slot == 1) {
                // second finger
                state->second_press_time = get_ms();
            }
        } else {
            // released
            if (state->dragging) {
                virtual_pointer_send(state->virtual_pointer, 0);
                state->dragging = false;
            } else if (slot == 1) {
                // second finger
                uint64_t t1 =
                    state->second_press_time - state->first_press_time;
                uint64_t t2 = get_ms() - state->second_press_time;
                if (t1 > 200 && t2 < 100) {
                    virtual_pointer_send(state->virtual_pointer, 1);
                    state->dragging = true;
                    state->first_press_time = state->second_press_time = 0;
                }
            }
        }
    }
}

int main(void) {
    struct state state = {0};

    state.udev = udev_new();
    state.li = libinput_udev_create_context(&li_interface, NULL, state.udev);
    libinput_udev_assign_seat(state.li, "seat0");

    uv_loop_init(&state.loop);
    uv_poll_init(&state.loop, &state.li_watcher, libinput_get_fd(state.li));
    uv_poll_start(&state.li_watcher, UV_READABLE, handle_li);
    state.li_watcher.data = &state;

    handle_li(&state.li_watcher, 0, 0);
    uv_run(&state.loop, UV_RUN_DEFAULT);
    if (state.touchpad_evdev) {
        printf("Found touchpad: %s\n", libevdev_get_name(state.touchpad_evdev));
    } else {
        fprintf(stderr, "Touchpad is not found\n");
        return 1;
    }

    state.virtual_pointer = create_virtual_pointer();
    if (!state.virtual_pointer) {
        fprintf(stderr, "Failed to create virtual pointer\n");
        return 1;
    }

    uv_poll_init(&state.loop, &state.touchpad_watcher,
                 libevdev_get_fd(state.touchpad_evdev));
    uv_poll_start(&state.touchpad_watcher, UV_READABLE, handle_touchpad);
    state.touchpad_watcher.data = &state;

    uv_run(&state.loop, UV_RUN_DEFAULT);

    return 0;
}
