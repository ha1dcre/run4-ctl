// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eugen Schott
#include "r4p.h" /* include order must not change C linkage */
#include "run4ctl.h"
#include <cassert>
static int transmit(void *, const uint8_t *, size_t) { return 1; }
int main()
{
    r4ctl_t device;
    r4ctl_callbacks_t callbacks = {};
    callbacks.send_sysex = transmit;
    assert(r4ctl_init(&device, &callbacks, 1, 0));
    r4ctl_set_link(&device, 1);
    r4ctl_poll(&device, 0);
    uint8_t bytes[16];
    assert(r4p_sync_req(bytes, sizeof bytes) == 6);
}
