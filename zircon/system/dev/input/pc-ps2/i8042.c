// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/hidbus.h>
#include <hw/inout.h>

#include <fuchsia/hardware/input/c/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <hid/boot.h>
#include <hid/usages.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#define xprintf(fmt...) do {} while (0)

typedef struct i8042_device {
    mtx_t lock;
    hidbus_ifc_protocol_t ifc;
    void* cookie;

    zx_handle_t irq;
    thrd_t irq_thread;

    int last_code;

    fuchsia_hardware_input_BootProtocol type;
    union {
        hid_boot_kbd_report_t kbd;
        hid_boot_mouse_report_t mouse;
    } report;
} i8042_device_t;

static inline bool is_kbd_modifier(uint8_t usage) {
    return (usage >= HID_USAGE_KEY_LEFT_CTRL && usage <= HID_USAGE_KEY_RIGHT_GUI);
}

#define MOD_SET 1
#define MOD_EXISTS 2
#define MOD_ROLLOVER 3
static int i8042_modifier_key(i8042_device_t* dev, uint8_t mod, bool down) {
    int bit = mod - HID_USAGE_KEY_LEFT_CTRL;
    if (bit < 0 || bit > 7) return MOD_ROLLOVER;
    if (down) {
        if (dev->report.kbd.modifier & 1 << bit) {
            return MOD_EXISTS;
        } else {
            dev->report.kbd.modifier |= 1 << bit;
        }
    } else {
        dev->report.kbd.modifier &= ~(1 << bit);
    }
    return MOD_SET;
}

#define KEY_ADDED 1
#define KEY_EXISTS 2
#define KEY_ROLLOVER 3
static int i8042_add_key(i8042_device_t* dev, uint8_t usage) {
    for (int i = 0; i < 6; i++) {
        if (dev->report.kbd.usage[i] == usage) return KEY_EXISTS;
        if (dev->report.kbd.usage[i] == 0) {
            dev->report.kbd.usage[i] = usage;
            return KEY_ADDED;
        }
    }
    return KEY_ROLLOVER;
}

#define KEY_REMOVED 1
#define KEY_NOT_FOUND 2
static int i8042_rm_key(i8042_device_t* dev, uint8_t usage) {
    int idx = -1;
    for (int i = 0; i < 6; i++) {
        if (dev->report.kbd.usage[i] == usage) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return KEY_NOT_FOUND;
    for (int i = idx; i < 5; i++) {
        dev->report.kbd.usage[i] = dev->report.kbd.usage[i+1];
    }
    dev->report.kbd.usage[5] = 0;
    return KEY_REMOVED;
}

#define I8042_COMMAND_REG 0x64
#define I8042_STATUS_REG 0x64
#define I8042_DATA_REG 0x60

#define ISA_IRQ_KEYBOARD 0x1
#define ISA_IRQ_MOUSE 0x0c

static inline int i8042_read_data(void) {
    return inp(I8042_DATA_REG);
}

static inline int i8042_read_status(void) {
    return inp(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val) {
    outp(I8042_DATA_REG, val);
}

static inline void i8042_write_command(int val) {
    outp(I8042_COMMAND_REG, val);
}

/*
 * timeout in milliseconds
 */
#define I8042_CTL_TIMEOUT 500

/*
 * status register bits
 */
#define I8042_STR_PARITY 0x80
#define I8042_STR_TIMEOUT 0x40
#define I8042_STR_AUXDATA 0x20
#define I8042_STR_KEYLOCK 0x10
#define I8042_STR_CMDDAT 0x08
#define I8042_STR_MUXERR 0x04
#define I8042_STR_IBF 0x02
#define I8042_STR_OBF 0x01

/*
 * control register bits
 */
#define I8042_CTR_KBDINT 0x01
#define I8042_CTR_AUXINT 0x02
#define I8042_CTR_IGNKEYLK 0x08
#define I8042_CTR_KBDDIS 0x10
#define I8042_CTR_AUXDIS 0x20
#define I8042_CTR_XLATE 0x40

/*
 * commands
 */
#define I8042_CMD_CTL_RCTR 0x0120
#define I8042_CMD_CTL_WCTR 0x1060
#define I8042_CMD_CTL_TEST 0x01aa
#define I8042_CMD_CTL_AUX  0x00d4

// Identity response will be ACK + 0, 1, or 2 bytes
#define I8042_CMD_IDENTIFY 0x03f2
#define I8042_CMD_SCAN_DIS 0x01f5
#define I8042_CMD_SCAN_EN 0x01f4

#define I8042_CMD_CTL_KBD_DIS 0x00ad
#define I8042_CMD_CTL_KBD_EN 0x00ae
#define I8042_CMD_CTL_KBD_TEST 0x01ab
#define I8042_CMD_KBD_MODE 0x01f0

#define I8042_CMD_CTL_MOUSE_DIS 0x00a7
#define I8042_CMD_CTL_MOUSE_EN 0x00a8
#define I8042_CMD_CTL_MOUSE_TEST 0x01a9

/*
 * used for flushing buffers. the i8042 internal buffer shoudn't exceed this.
 */
#define I8042_BUFFER_LENGTH 32

static const uint8_t kbd_hid_report_desc[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x07,  //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,  //   Usage Minimum (0xE0)
    0x29, 0xE7,  //   Usage Maximum (0xE7)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x05, 0x08,  //   Usage Page (LEDs)
    0x19, 0x01,  //   Usage Minimum (Num Lock)
    0x29, 0x05,  //   Usage Maximum (Kana)
    0x91, 0x02,  //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,  //   Usage Minimum (0x00)
    0x29, 0x65,  //   Usage Maximum (0x65)
    0x81, 0x00,  //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,        // End Collection
};

static const uint8_t pc_set1_usage_map[128] = {
    /* 0x00 */ 0, HID_USAGE_KEY_ESC, HID_USAGE_KEY_1, HID_USAGE_KEY_2,
    /* 0x04 */ HID_USAGE_KEY_3, HID_USAGE_KEY_4, HID_USAGE_KEY_5, HID_USAGE_KEY_6,
    /* 0x08 */ HID_USAGE_KEY_7, HID_USAGE_KEY_8, HID_USAGE_KEY_9, HID_USAGE_KEY_0,
    /* 0x0c */ HID_USAGE_KEY_MINUS, HID_USAGE_KEY_EQUAL, HID_USAGE_KEY_BACKSPACE, HID_USAGE_KEY_TAB,
    /* 0x10 */ HID_USAGE_KEY_Q, HID_USAGE_KEY_W, HID_USAGE_KEY_E, HID_USAGE_KEY_R,
    /* 0x14 */ HID_USAGE_KEY_T, HID_USAGE_KEY_Y, HID_USAGE_KEY_U, HID_USAGE_KEY_I,
    /* 0x18 */ HID_USAGE_KEY_O, HID_USAGE_KEY_P, HID_USAGE_KEY_LEFTBRACE, HID_USAGE_KEY_RIGHTBRACE,
    /* 0x1c */ HID_USAGE_KEY_ENTER, HID_USAGE_KEY_LEFT_CTRL, HID_USAGE_KEY_A, HID_USAGE_KEY_S,
    /* 0x20 */ HID_USAGE_KEY_D, HID_USAGE_KEY_F, HID_USAGE_KEY_G, HID_USAGE_KEY_H,
    /* 0x24 */ HID_USAGE_KEY_J, HID_USAGE_KEY_K, HID_USAGE_KEY_L, HID_USAGE_KEY_SEMICOLON,
    /* 0x28 */ HID_USAGE_KEY_APOSTROPHE, HID_USAGE_KEY_GRAVE, HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_BACKSLASH,
    /* 0x2c */ HID_USAGE_KEY_Z, HID_USAGE_KEY_X, HID_USAGE_KEY_C, HID_USAGE_KEY_V,
    /* 0x30 */ HID_USAGE_KEY_B, HID_USAGE_KEY_N, HID_USAGE_KEY_M, HID_USAGE_KEY_COMMA,
    /* 0x34 */ HID_USAGE_KEY_DOT, HID_USAGE_KEY_SLASH, HID_USAGE_KEY_RIGHT_SHIFT, HID_USAGE_KEY_KP_ASTERISK,
    /* 0x38 */ HID_USAGE_KEY_LEFT_ALT, HID_USAGE_KEY_SPACE, HID_USAGE_KEY_CAPSLOCK, HID_USAGE_KEY_F1,
    /* 0x3c */ HID_USAGE_KEY_F2, HID_USAGE_KEY_F3, HID_USAGE_KEY_F4, HID_USAGE_KEY_F5,
    /* 0x40 */ HID_USAGE_KEY_F6, HID_USAGE_KEY_F7, HID_USAGE_KEY_F8, HID_USAGE_KEY_F9,
    /* 0x44 */ HID_USAGE_KEY_F10, HID_USAGE_KEY_NUMLOCK, HID_USAGE_KEY_SCROLLLOCK, HID_USAGE_KEY_KP_7,
    /* 0x48 */ HID_USAGE_KEY_KP_8, HID_USAGE_KEY_KP_9, HID_USAGE_KEY_KP_MINUS, HID_USAGE_KEY_KP_4,
    /* 0x4c */ HID_USAGE_KEY_KP_5, HID_USAGE_KEY_KP_6, HID_USAGE_KEY_KP_PLUS, HID_USAGE_KEY_KP_1,
    /* 0x50 */ HID_USAGE_KEY_KP_2, HID_USAGE_KEY_KP_3, HID_USAGE_KEY_KP_0, HID_USAGE_KEY_KP_DOT,
    /* 0x54 */ 0, 0, 0, HID_USAGE_KEY_F11,
    /* 0x58 */ HID_USAGE_KEY_F12, 0, 0, 0,
};

static const uint8_t pc_set1_usage_map_e0[128] = {
    /* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x08 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x10 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x18 */ 0, 0, 0, 0, HID_USAGE_KEY_KP_ENTER, HID_USAGE_KEY_RIGHT_CTRL, 0, 0,
    /* 0x20 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x28 */ 0, 0, 0, 0, 0, 0, HID_USAGE_KEY_VOL_DOWN, 0,
    /* 0x30 */ HID_USAGE_KEY_VOL_UP, 0, 0, 0, 0, HID_USAGE_KEY_KP_SLASH, 0, HID_USAGE_KEY_PRINTSCREEN,
    /* 0x38 */ HID_USAGE_KEY_RIGHT_ALT, 0, 0, 0, 0, 0, 0, 0,
    /* 0x40 */ 0, 0, 0, 0, 0, 0, 0, HID_USAGE_KEY_HOME,
    /* 0x48 */ HID_USAGE_KEY_UP, HID_USAGE_KEY_PAGEUP, 0, HID_USAGE_KEY_LEFT, 0, HID_USAGE_KEY_RIGHT, 0, HID_USAGE_KEY_END,
    /* 0x50 */ HID_USAGE_KEY_DOWN, HID_USAGE_KEY_PAGEDOWN, HID_USAGE_KEY_INSERT, HID_USAGE_KEY_DELETE, 0, 0, 0, 0,
    /* 0x58 */ 0, 0, 0, HID_USAGE_KEY_LEFT_GUI, HID_USAGE_KEY_RIGHT_GUI, 0 /* MENU */, 0, 0,
};

static const uint8_t mouse_hid_report_desc[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x03,        //     Usage Maximum (0x03)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (129)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

static const hid_boot_kbd_report_t report_err_rollover = {
    .modifier = 1,
    .usage = {1, 1, 1, 1, 1, 1 }
};

static int i8042_wait_read(void) {
    int i = 0;
    while ((~i8042_read_status() & I8042_STR_OBF) && (i < I8042_CTL_TIMEOUT)) {
        usleep(10);
        i++;
    }
    return -(i == I8042_CTL_TIMEOUT);
}

static int i8042_wait_write(void) {
    int i = 0;
    while ((i8042_read_status() & I8042_STR_IBF) && (i < I8042_CTL_TIMEOUT)) {
        usleep(10);
        i++;
    }
    return -(i == I8042_CTL_TIMEOUT);
}

static int i8042_flush(void) {
    unsigned char data __UNUSED;
    int i = 0;

    while ((i8042_read_status() & I8042_STR_OBF) && (i++ < I8042_BUFFER_LENGTH)) {
        usleep(10);
        data = i8042_read_data();
    }

    return i;
}

static int i8042_command_data(uint8_t* param, int command) {
    int retval = 0, i = 0;

    for (i = 0; i < ((command >> 12) & 0xf); i++) {
        if ((retval = i8042_wait_write())) {
            break;
        }

        i8042_write_data(param[i]);
    }

    int expected = (command >> 8) & 0xf;
    if (!retval) {
        for (i = 0; i < expected; i++) {
            if ((retval = i8042_wait_read())) {
                xprintf("i8042: timeout reading; got %d bytes\n", i);
                return i;
            }

            // TODO: do we need to distinguish keyboard and aux data?
            param[i] = i8042_read_data();
        }
    }

    return retval ? retval : expected;
}

static int i8042_command(uint8_t* param, int command) {
    xprintf("i8042 ctl command 0x%04x\n", command & 0xffff);
    int retval = 0;

    retval = i8042_wait_write();
    if (!retval) {
        i8042_write_command(command & 0xff);
    }

    if (!retval) {
        retval = i8042_command_data(param, command);
    }

    return retval;
}

static int i8042_selftest(void) {
    uint8_t param;
    int i = 0;
    do {
        if (i8042_command(&param, I8042_CMD_CTL_TEST) < 0) {
            return -1;
        }
        if (param == 0x55)
            return 0;
        usleep(50 * 1000);
    } while (i++ < 5);
    return -1;
}

static int i8042_dev_command(uint8_t* param, int command) {
    xprintf("i8042 dev command 0x%04x\n", command & 0xffff);
    int retval = 0;

    retval = i8042_wait_write();
    if (!retval) {
        i8042_write_data(command & 0xff);
    }

    if (!retval) {
        retval = i8042_command_data(param, command);
    }

    return retval;
}

static int i8042_aux_command(uint8_t* param, int command) {
    xprintf("i8042 aux command\n");
    int retval = 0;

    retval = i8042_wait_write();
    if (!retval) {
        i8042_write_command(I8042_CMD_CTL_AUX);
    }

    if (!retval) {
        return i8042_dev_command(param, command);
    }

    return retval;
}

static void i8042_process_scode(i8042_device_t* dev, uint8_t scode, unsigned int flags) {
    // is this a multi code sequence?
    bool multi = (dev->last_code == 0xe0);

    // update the last received code
    dev->last_code = scode;

    // save the key up event bit
    bool key_up = !!(scode & 0x80);
    scode &= 0x7f;

    // translate the key based on our translation table
    uint8_t usage;
    if (multi) {
        usage = pc_set1_usage_map_e0[scode];
    } else {
        usage = pc_set1_usage_map[scode];
    }
    if (!usage) return;

    bool rollover = false;
    if (is_kbd_modifier(usage)) {
        switch (i8042_modifier_key(dev, usage, !key_up)) {
        case MOD_EXISTS:
            return;
        case MOD_ROLLOVER:
            rollover = true;
            break;
        case MOD_SET:
        default:
            break;
        }
    } else if (key_up) {
        if (i8042_rm_key(dev, usage) != KEY_REMOVED) {
            rollover = true;
        }
    } else {
        switch (i8042_add_key(dev, usage)) {
        case KEY_EXISTS:
            return;
        case KEY_ROLLOVER:
            rollover = true;
            break;
        case KEY_ADDED:
        default:
            break;
        }
    }

    //cprintf("i8042: scancode=0x%x, keyup=%u, multi=%u: usage=0x%x\n", scode, !!key_up, multi, usage);

    const hid_boot_kbd_report_t* report = rollover ? &report_err_rollover : &dev->report.kbd;
    mtx_lock(&dev->lock);
    if (dev->ifc.ops) {
        hidbus_ifc_io_queue(&dev->ifc, (const uint8_t*)report, sizeof(*report));
    }
    mtx_unlock(&dev->lock);
}

static void i8042_process_mouse(i8042_device_t* dev, uint8_t data, unsigned int flags) {
    switch (dev->last_code) {
    case 0:
        if (!(data & 0x08)) {
            // The first byte always has bit 3 set, so skip this packet.
            return;
        }
        dev->report.mouse.buttons = data;
        break;
    case 1: {
        int state = dev->report.mouse.buttons;
        int d = data;
        dev->report.mouse.rel_x = d - ((state << 4) & 0x100);
        break;
        }
    case 2: {
        int state = dev->report.mouse.buttons;
        int d = data;
        // PS/2 maps the y-axis backwards so invert the rel_y value
        dev->report.mouse.rel_y = ((state << 3) & 0x100) - d;
        dev->report.mouse.buttons &= 0x7;

        mtx_lock(&dev->lock);
        if (dev->ifc.ops) {
            hidbus_ifc_io_queue(&dev->ifc, (const uint8_t*)&dev->report.mouse,
                            sizeof(dev->report.mouse));
        }
        mtx_unlock(&dev->lock);
        memset(&dev->report.mouse, 0, sizeof(dev->report.mouse));
        break;
        }
    }
    dev->last_code = (dev->last_code + 1) % 3;
}

static int i8042_irq_thread(void* arg) {
    i8042_device_t* device = (i8042_device_t*)arg;

    // enable I/O port access
    // TODO
    zx_status_t status;
    // Please do not use get_root_resource() in new code. See ZX-1497.
    status = zx_ioports_request(get_root_resource(), I8042_COMMAND_REG, 1);
    if (status)
        return 0;
    // Please do not use get_root_resource() in new code. See ZX-1497.
    status = zx_ioports_request(get_root_resource(), I8042_DATA_REG, 1);
    if (status)
        return 0;

    for (;;) {
        status = zx_interrupt_wait(device->irq, NULL);
        if (status != ZX_OK) {
            break;
        }
        // keep handling status on the controller until no bits are set we care about
        bool retry;
        do {
            retry = false;

            uint8_t str = i8042_read_status();

            // check for incoming data from the controller
            // TODO: deal with potential race between IRQ1 and IRQ12
            if (str & I8042_STR_OBF) {
                uint8_t data = i8042_read_data();
                // TODO: should we check (str & I8042_STR_AUXDATA) before
                // handling this byte?
                if (device->type == fuchsia_hardware_input_BootProtocol_KBD) {
                    i8042_process_scode(device, data,
                                        ((str & I8042_STR_PARITY) ? I8042_STR_PARITY : 0) |
                                        ((str & I8042_STR_TIMEOUT) ? I8042_STR_TIMEOUT : 0));
                } else if (device->type == fuchsia_hardware_input_BootProtocol_MOUSE) {
                    i8042_process_mouse(device, data, 0);
                }
                retry = true;
            }
            // TODO check other status bits here
        } while (retry);
    }
    return 0;
}

static zx_status_t i8042_setup(uint8_t* ctr) {
    // enable I/O port access
    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx_status_t status = zx_ioports_request(get_root_resource(), I8042_COMMAND_REG, 1);
    if (status)
        return status;
    // Please do not use get_root_resource() in new code. See ZX-1497.
    status = zx_ioports_request(get_root_resource(), I8042_DATA_REG, 1);
    if (status)
        return status;

    // initialize hardware
    i8042_command(NULL, I8042_CMD_CTL_KBD_DIS);
    i8042_command(NULL, I8042_CMD_CTL_MOUSE_DIS);
    i8042_flush();

    if (i8042_command(ctr, I8042_CMD_CTL_RCTR) < 0)
        return -1;

    xprintf("i8042 controller register: 0x%02x\n", *ctr);
    bool have_mouse = !!(*ctr & I8042_CTR_AUXDIS);
    // disable IRQs and translation
    *ctr &= ~(I8042_CTR_KBDINT | I8042_CTR_AUXINT | I8042_CTR_XLATE);
    if (i8042_command(ctr, I8042_CMD_CTL_WCTR) < 0)
        return -1;

    if (i8042_selftest() < 0) {
        printf("i8042 self-test failed\n");
        return -1;
    }

    uint8_t resp = 0;
    if (i8042_command(&resp, I8042_CMD_CTL_KBD_TEST) < 0)
        return -1;
    if (resp != 0x00) {
        printf("i8042 kbd test failed: 0x%02x\n", resp);
        return -1;
    }
    if (have_mouse) {
        resp = 0;
        if (i8042_command(&resp, I8042_CMD_CTL_MOUSE_TEST) < 0)
            return -1;
        if (resp != 0x00) {
            printf("i8042 mouse test failed: 0x%02x\n", resp);
            return -1;
        }
    }
    return ZX_OK;
}

static void i8042_identify(int (*cmd)(uint8_t* param, int command)) {
    uint8_t resp[3];
    if (cmd(resp, I8042_CMD_SCAN_DIS) < 0) return;
    resp[0] = 0;
    int ident_sz = cmd(resp, I8042_CMD_IDENTIFY);
    if (ident_sz < 0) return;
    printf("i8042 device ");
    switch (ident_sz) {
    case 1:
        printf("(unknown)");
        break;
    case 2:
        printf("0x%02x", resp[1]);
        break;
    case 3:
        printf("0x%02x 0x%02x", resp[1], resp[2]);
        break;
    default:
        printf("failed to respond to IDENTIFY");
    }
    printf("\n");
    cmd(resp, I8042_CMD_SCAN_EN);
}

static zx_status_t i8042_query(void* ctx, uint32_t options, hid_info_t* info) {
    i8042_device_t* i8042 = ctx;
    info->dev_num = i8042->type;  // use the type for the device number for now
    info->device_class = i8042->type;
    info->boot_device = true;
    return ZX_OK;
}

static zx_status_t i8042_start(void* ctx, const hidbus_ifc_protocol_t* ifc) {
    i8042_device_t* i8042 = ctx;
    mtx_lock(&i8042->lock);
    if (i8042->ifc.ops != NULL) {
        mtx_unlock(&i8042->lock);
        return ZX_ERR_ALREADY_BOUND;
    }
    i8042->ifc = *ifc;
    mtx_unlock(&i8042->lock);
    return ZX_OK;
}

static void i8042_stop(void* ctx) {
    i8042_device_t* i8042 = ctx;
    mtx_lock(&i8042->lock);
    i8042->ifc.ops = NULL;
    i8042->ifc.ctx = NULL;
    mtx_unlock(&i8042->lock);
}

static zx_status_t i8042_get_descriptor(void* ctx, uint8_t desc_type,
        void** data, size_t* len) {
    if (data == NULL || len == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
        return ZX_ERR_NOT_FOUND;
    }

    i8042_device_t* device = ctx;
    const uint8_t* buf = NULL;
    size_t buflen = 0;
    if (device->type == fuchsia_hardware_input_BootProtocol_KBD) {
        buf = (void*)&kbd_hid_report_desc;
        buflen = sizeof(kbd_hid_report_desc);
    } else if (device->type == fuchsia_hardware_input_BootProtocol_MOUSE) {
        buf = (void*)&mouse_hid_report_desc;
        buflen = sizeof(mouse_hid_report_desc);
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }

    *data = malloc(buflen);
    *len = buflen;
    memcpy(*data, buf, buflen);
    return ZX_OK;
}

static zx_status_t i8042_get_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len, size_t* out_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t i8042_set_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
        const void* data, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t i8042_get_idle(void* ctx, uint8_t rpt_type, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t i8042_set_idle(void* ctx, uint8_t rpt_type, uint8_t duration) {
    return ZX_OK;
}

static zx_status_t i8042_get_protocol(void* ctx, uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t i8042_set_protocol(void* ctx, uint8_t protocol) {
    return ZX_OK;
}

static hidbus_protocol_ops_t hidbus_ops = {
    .query = i8042_query,
    .start = i8042_start,
    .stop = i8042_stop,
    .get_descriptor = i8042_get_descriptor,
    .get_report = i8042_get_report,
    .set_report = i8042_set_report,
    .get_idle = i8042_get_idle,
    .set_idle = i8042_set_idle,
    .get_protocol = i8042_get_protocol,
    .set_protocol = i8042_set_protocol,
};

static void i8042_cleanup_irq_thread(i8042_device_t* dev) {
    zx_interrupt_destroy(dev->irq);
    thrd_join(dev->irq_thread, NULL);
    zx_handle_close(dev->irq);
}

static void i8042_release(void* ctx) {
    i8042_device_t* i8042 = ctx;
    i8042_cleanup_irq_thread(i8042);
    free(i8042);
}

static zx_protocol_device_t i8042_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = i8042_release,
};

static zx_status_t i8042_dev_init(i8042_device_t* dev, const char* name, zx_device_t* parent) {
    // enable device port
    int cmd = dev->type == fuchsia_hardware_input_BootProtocol_KBD ? I8042_CMD_CTL_KBD_DIS
                                                                   : I8042_CMD_CTL_MOUSE_DIS;
    i8042_command(NULL, cmd);

    // TODO: use identity to determine device type, rather than assuming aux ==
    // mouse
    i8042_identify(dev->type == fuchsia_hardware_input_BootProtocol_KBD ? i8042_dev_command
                                                                        : i8042_aux_command);

    cmd = dev->type == fuchsia_hardware_input_BootProtocol_KBD ? I8042_CMD_CTL_KBD_EN
                                                               : I8042_CMD_CTL_MOUSE_EN;
    i8042_command(NULL, cmd);

    uint32_t interrupt =
        dev->type == fuchsia_hardware_input_BootProtocol_KBD ? ISA_IRQ_KEYBOARD : ISA_IRQ_MOUSE;

    // Please do not use get_root_resource() in new code. See ZX-1497.
    zx_status_t status = zx_interrupt_create(get_root_resource(), interrupt,
                        ZX_INTERRUPT_REMAP_IRQ, &dev->irq);
    if (status != ZX_OK) {
        return status;
    }

        // create irq thread
    const char* tname =
        dev->type == fuchsia_hardware_input_BootProtocol_KBD ? "i8042-kbd-irq" : "i8042-mouse-irq";
    int ret = thrd_create_with_name(&dev->irq_thread, i8042_irq_thread, dev, tname);
    if (ret != thrd_success) {
        zx_handle_close(dev->irq);
        return ZX_ERR_BAD_STATE;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = dev,
        .ops = &i8042_dev_proto,
        .proto_id = ZX_PROTOCOL_HIDBUS,
        .proto_ops = &hidbus_ops,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        i8042_cleanup_irq_thread(dev);
    }
    return status;
}

static int i8042_init_thread(void* arg) {
    zx_device_t* parent = arg;
    uint8_t ctr = 0;
    zx_status_t status = i8042_setup(&ctr);
    if (status != ZX_OK) {
        return status;
    }

    bool have_mouse = !!(ctr & I8042_CTR_AUXDIS);

    // turn on translation
    ctr |= I8042_CTR_XLATE;

    // enable devices and irqs
    ctr &= ~I8042_CTR_KBDDIS;
    ctr |= I8042_CTR_KBDINT;
    if (have_mouse) {
        ctr &= ~I8042_CTR_AUXDIS;
        ctr |= I8042_CTR_AUXINT;
    }

    if (i8042_command(&ctr, I8042_CMD_CTL_WCTR) < 0)
        return -1;

    // create keyboard device
    i8042_device_t* kbd_device = calloc(1, sizeof(i8042_device_t));
    if (!kbd_device)
        return ZX_ERR_NO_MEMORY;

    mtx_init(&kbd_device->lock, mtx_plain);
    kbd_device->type = fuchsia_hardware_input_BootProtocol_KBD;
    status = i8042_dev_init(kbd_device, "i8042-keyboard", parent);
    if (status != ZX_OK) {
        free(kbd_device);
    }

    // Mouse
    if (have_mouse) {
        i8042_device_t* mouse_device = NULL;
        mouse_device = calloc(1, sizeof(i8042_device_t));
        if (mouse_device) {
            mtx_init(&mouse_device->lock, mtx_plain);
            mouse_device->type = fuchsia_hardware_input_BootProtocol_MOUSE;
            status = i8042_dev_init(mouse_device, "i8042-mouse", parent);
            if (status != ZX_OK) {
                free(mouse_device);
            }
        }
    }

    xprintf("initialized i8042 driver\n");

    return ZX_OK;
}

static zx_status_t i8042_bind(void* ctx, zx_device_t* parent) {
    thrd_t t;
    int rc = thrd_create_with_name(&t, i8042_init_thread, parent, "i8042-init");
    return rc;
}

static zx_driver_ops_t i8042_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = i8042_bind,
};

ZIRCON_DRIVER_BEGIN(i8042, i8042_driver_ops, "zircon", "0.1", 6)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ACPI),
    BI_GOTO_IF(NE, BIND_ACPI_HID_0_3, 0x504e5030, 0), // PNP0303\0
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, 0x33303300),
    BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_ACPI_CID_0_3, 0x504e5030), // PNP0303\0
    BI_MATCH_IF(EQ, BIND_ACPI_CID_4_7, 0x33303300),
ZIRCON_DRIVER_END(i8042)
