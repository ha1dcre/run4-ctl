/* SPDX-License-Identifier: GPL-3.0-only
 * Native unit tests of the RUN4 CTL codec: cc tests/r4p_test.c src/r4p.c (make test) */
#include <stdio.h>
#include <string.h>
#include "../src/r4p.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t buf[1024];

static size_t enc(const r4p_msg_t *m) { return r4p_encode(buf, sizeof buf, m); }
static int round_trip(const r4p_msg_t *m, r4p_msg_t *back)
{
    size_t n = enc(m);
    return n ? r4p_parse(buf, n, back) : -9;
}
static void fill_page(r4p_page_t *p)
{
    int i;
    memset(p, 0, sizeof *p);
    strcpy(p->title, "TAPE"); strcpy(p->track, "T1");
    p->r = 127; p->g = 64; p->b = 0;
    p->page_idx = 1; p->page_count = 5; p->bank_idx = 0; p->bank_count = 2;
    for (i = 0; i < 4; i++) {
        snprintf(p->slot[i].name, sizeof p->slot[i].name, "SLOT%d", i);
        p->slot[i].kind = (uint8_t)(i + 1);
        p->slot[i].value = (uint16_t)(i * 5000);
        snprintf(p->slot[i].text, sizeof p->slot[i].text, "%d.5 dB", i);
    }
}

static void t_simple(void)
{
    r4p_msg_t m, b;
    int i;
    m.cmd = R4P_HELLO; m.u.hello.proto = 1; m.u.hello.fw_major = 0; m.u.hello.fw_minor = 1; m.u.hello.mode = 1; m.u.hello.minor = 0;
    CHECK(round_trip(&m, &b) == 1 && b.cmd == R4P_HELLO && b.u.hello.proto == 1 && b.u.hello.fw_minor == 1 && b.u.hello.mode == 1);
    for (i = 0; i < R4P_NBTN; i++) {
        m.cmd = R4P_BTN; m.u.btn.id = (uint8_t)i; m.u.btn.state = (uint8_t)(i & 1);
        CHECK(round_trip(&m, &b) == 1 && b.u.btn.id == i && b.u.btn.state == (i & 1));
    }
    m.u.btn.id = 14; m.u.btn.state = 1; CHECK(enc(&m) == 0);          /* no such button */
    m.u.btn.id = 0; m.u.btn.state = 2; CHECK(enc(&m) == 0);
    for (i = 0; i < R4P_NENC; i++) {
        int d;
        for (d = -63; d <= 63; d += 21) {
            m.cmd = R4P_ENC; m.u.enc.id = (uint8_t)i; m.u.enc.delta = (int8_t)d;
            CHECK(round_trip(&m, &b) == 1 && b.u.enc.id == i && b.u.enc.delta == d);
        }
    }
    m.cmd = R4P_ENC; m.u.enc.id = 3; m.u.enc.delta = 0;               /* wire form of 0 is 0x40 */
    CHECK(enc(&m) == 8 && buf[6] == 0x40);
    m.u.enc.delta = 1; enc(&m); CHECK(buf[6] == 0x41);
    m.u.enc.delta = -1; enc(&m); CHECK(buf[6] == 0x3F);
    m.u.enc.delta = 100; enc(&m); CHECK(buf[6] == 0x40 + 63);         /* clamped */
    m.u.enc.delta = -100; enc(&m); CHECK(buf[6] == 0x40 - 63);
    m.u.enc.id = 7; CHECK(enc(&m) == 0);
    m.cmd = R4P_POT;
    for (i = 0; i < 3; i++) {
        static const uint16_t V[3] = {0, 16383, 8192};
        m.u.pot.value = V[i];
        CHECK(round_trip(&m, &b) == 1 && b.u.pot.value == V[i]);
    }
    m.u.pot.value = 16384; CHECK(enc(&m) == 0);
    m.cmd = R4P_SYNC_REQ; CHECK(round_trip(&m, &b) == 1 && b.cmd == R4P_SYNC_REQ);
    m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 1; m.u.ack.minor = 0; CHECK(round_trip(&m, &b) == 1 && b.u.ack.proto == 1);
    CHECK(r4p_sync_req(buf, sizeof buf) == 6);
}

static void t_page_value_status_toast(void)
{
    r4p_msg_t m, b;
    int i;
    m.cmd = R4P_PAGE; fill_page(&m.u.page);
    CHECK(round_trip(&m, &b) == 1);
    CHECK(!strcmp(b.u.page.title, "TAPE") && !strcmp(b.u.page.track, "T1") && b.u.page.r == 127 && b.u.page.g == 64);
    CHECK(b.u.page.page_idx == 1 && b.u.page.page_count == 5 && b.u.page.bank_count == 2);
    for (i = 0; i < 4; i++)
        CHECK(!strcmp(b.u.page.slot[i].name, m.u.page.slot[i].name) && b.u.page.slot[i].kind == i + 1 &&
              b.u.page.slot[i].value == i * 5000 && !strcmp(b.u.page.slot[i].text, m.u.page.slot[i].text));
    m.u.page.slot[0].value = 16383; m.u.page.slot[1].value = 0;       /* 14-bit edges */
    CHECK(round_trip(&m, &b) == 1 && b.u.page.slot[0].value == 16383 && b.u.page.slot[1].value == 0);
    m.u.page.slot[0].kind = 5; CHECK(enc(&m) == 0);                    /* bad kind */
    m.u.page.slot[0].kind = 1; m.u.page.r = 128; CHECK(enc(&m) == 0);  /* colour is 7 bit */

    m.cmd = R4P_VALUE; m.u.value.slot = 3; m.u.value.value = 16383; strcpy(m.u.value.text, "+12.0 dB"); m.u.value.flags = 0;
    CHECK(round_trip(&m, &b) == 1 && b.u.value.slot == 3 && b.u.value.value == 16383 && !strcmp(b.u.value.text, "+12.0 dB"));
    m.u.value.value = 0; CHECK(round_trip(&m, &b) == 1 && b.u.value.value == 0);
    m.u.value.slot = 4; CHECK(enc(&m) == 0);

    m.cmd = R4P_STATUS; m.u.status.bpm10 = 1203; m.u.status.bar = 16383; m.u.status.beat = 15; m.u.status.flags = 7;
    CHECK(round_trip(&m, &b) == 1 && b.u.status.bpm10 == 1203 && b.u.status.bar == 16383 && b.u.status.beat == 15 && b.u.status.flags == 7);
    m.u.status.bpm10 = 0; m.u.status.bar = 0; m.u.status.beat = 0; m.u.status.flags = 0;
    CHECK(round_trip(&m, &b) == 1 && b.u.status.bpm10 == 0 && b.u.status.bar == 0);
    m.u.status.beat = 16; CHECK(enc(&m) == 0);

    m.cmd = R4P_TOAST; strcpy(m.u.toast.text, "SAVED"); m.u.toast.dur100ms = 20;
    CHECK(round_trip(&m, &b) == 1 && !strcmp(b.u.toast.text, "SAVED") && b.u.toast.dur100ms == 20);
    m.u.toast.text[0] = 0; CHECK(round_trip(&m, &b) == 1 && b.u.toast.text[0] == 0);   /* empty string */
}

static void t_strings(void)
{
    r4p_msg_t m, b;
    char l1[64];
    size_t n;
    m.cmd = R4P_TOAST; m.u.toast.dur100ms = 1;
    strcpy(m.u.toast.text, "T\xD3" "LF");                              /* TOLF with O acute, Latin-1 */
    n = enc(&m);
    /* F0 7D 52 34 46 | len 4 | 'T' 7F 53 'L' 'F' -> len counts characters, the escape is two bytes */
    CHECK(n == 5 + 1 + 5 + 1 + 1 + 0 && buf[5] == 4 && buf[6] == 'T' && buf[7] == 0x7F && buf[8] == 0x53 && buf[9] == 'L');
    CHECK(round_trip(&m, &b) == 1 && !strcmp(b.u.toast.text, "T\xD3" "LF"));
    CHECK(r4p_utf8_to_latin1(l1, sizeof l1, "T\xC3\x93LF \xE2\x82\xAC") == 6 && !strcmp(l1, "T\xD3" "LF ?"));   /* UTF-8 O acute; euro -> ? */
    {   /* all 128 upper Latin-1 characters survive the escape */
        int c;
        for (c = 0x80; c <= 0xFF; c++) {
            m.u.toast.text[0] = (char)c; m.u.toast.text[1] = 0;
            CHECK(round_trip(&m, &b) == 1 && (unsigned char)b.u.toast.text[0] == c);
        }
    }
    CHECK(r4p_fold(0xD3) == 'O' && r4p_fold('x') == 'x' && r4p_fold(0xE9) == 'e' && r4p_fold(0x85) == '?');
    {
        char f[8];
        r4p_fold_str(f, "T\xD3" "LF");
        CHECK(!strcmp(f, "TOLF"));
    }
    /* truncation: a string longer than 24 characters (here: no NUL in the whole field) goes out as 24 */
    memset(m.u.toast.text, 'A', 25);
    CHECK(round_trip(&m, &b) == 1 && strlen(b.u.toast.text) == 24 && buf[5] == 24);
    /* control characters are replaced, not passed */
    strcpy(m.u.toast.text, "A\x01" "B"); CHECK(round_trip(&m, &b) == 1 && !strcmp(b.u.toast.text, "A?B"));
}

static void t_max_page(void)
{
    r4p_msg_t m, b;
    size_t n;
    int i, k;
    m.cmd = R4P_PAGE;
    memset(&m.u.page, 0, sizeof m.u.page);
    for (k = 0; k < 24; k++)
        m.u.page.title[k] = m.u.page.track[k] = '\xD3';                 /* worst case: every character escaped */
    m.u.page.r = m.u.page.g = m.u.page.b = 127;
    for (i = 0; i < 4; i++) {
        for (k = 0; k < 24; k++)
            m.u.page.slot[i].name[k] = m.u.page.slot[i].text[k] = '\xE9';
        m.u.page.slot[i].kind = 4; m.u.page.slot[i].value = 16383;
    }
    n = enc(&m);
    printf("  max PAGE frame: %zu bytes (%zu between F0 and F7; limit %d)\n", n, n - 2, R4P_FRAME_MAX);
    CHECK(n == 515 && n - 2 < 640);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.page.slot[3].text[23] == '\xE9');
}

static void t_malformed(void)
{
    r4p_msg_t m, b;
    uint8_t f[600];
    size_t n, i;
    m.cmd = R4P_PAGE; fill_page(&m.u.page);
    n = enc(&m);
    memcpy(f, buf, n);
    CHECK(r4p_parse(f, n, &b) == 1);
    /* every truncation is rejected (with and without a closing F7), nothing crashes */
    for (i = 0; i < n - 1; i++) {
        uint8_t t[600];
        memcpy(t, f, i);
        t[i] = 0xF7;
        CHECK(r4p_parse(t, i + 1, &b) != 1);
    }
    CHECK(r4p_parse(f, n - 1, &b) == -1);                              /* no F7 */
    f[3] = 0x35; CHECK(r4p_parse_body(f + 1, n - 2, &b) == 0); f[3] = 0x34;   /* another header is "not ours" */
    f[1] = 0x7E; CHECK(r4p_parse(f, n, &b) == 0); f[1] = 0x7D;
    CHECK(r4p_parse_body((const uint8_t *)"\x7D\x53\x52\x01", 4, &b) == 0);   /* fm1-base's screen reader frame */
    CHECK(r4p_parse_body((const uint8_t *)"\x7D\x4C\x47", 3, &b) == 0);        /* fm1-base's crash readout */
    f[8] = 0x80; CHECK(r4p_parse(f, n, &b) == -1); f[8] = 'A';          /* an 8-bit byte */
    f[5] = 25; CHECK(r4p_parse(f, n, &b) == -1);                        /* string length 25 */
    f[5] = 4;
    f[4] = 0x7F; CHECK(r4p_parse(f, n, &b) == -1); f[4] = R4P_PAGE;     /* unknown command */
    f[4] = 0x20; CHECK(r4p_parse(f, n, &b) == -1); f[4] = R4P_PAGE;
    {   /* trailing garbage */
        uint8_t g[8] = {0xF0, 0x7D, 0x52, 0x34, R4P_SYNC_REQ, 0x00, 0xF7};
        CHECK(r4p_parse(g, 7, &b) == -1);
        g[5] = 0xF7; CHECK(r4p_parse(g, 6, &b) == 1);
    }
    {   /* a lone header, a bad escape, a control character in a string */
        uint8_t g[] = {0xF0, 0x7D, 0x52, 0x34, 0xF7};
        CHECK(r4p_parse(g, 5, &b) == -1);
        uint8_t t[] = {0xF0, 0x7D, 0x52, 0x34, R4P_TOAST, 1, 0x7F, 0x05, 0xF7};    /* 7F 05 = U+0085, legal */
        CHECK(r4p_parse(t, 9, &b) == -1);                               /* but the duration byte is missing */
        uint8_t u[] = {0xF0, 0x7D, 0x52, 0x34, R4P_TOAST, 1, 0x03, 0x05, 0xF7};    /* control character 3 */
        CHECK(r4p_parse(u, 9, &b) == -1);
    }
    {   /* out-of-range fields */
        uint8_t e[] = {0xF0, 0x7D, 0x52, 0x34, R4P_ENC, 3, 0x00, 0xF7};     /* -64 is not allowed */
        uint8_t k[] = {0xF0, 0x7D, 0x52, 0x34, R4P_BTN, 14, 1, 0xF7};
        CHECK(r4p_parse(e, 8, &b) == -1);
        CHECK(r4p_parse(k, 8, &b) == -1);
    }
    CHECK(r4p_parse(buf, 0, &b) == -1);
}

static void t_enc_acc(void)
{
    r4p_enc_acc_t a;
    int i;
    memset(&a, 0, sizeof a);
    r4p_enc_add(&a, 3, 1); r4p_enc_add(&a, 3, 1); r4p_enc_add(&a, 3, 1);
    CHECK(r4p_enc_take(&a, 3) == 3 && r4p_enc_take(&a, 3) == 0);        /* coalesced, then empty */
    r4p_enc_add(&a, 4, -2); r4p_enc_add(&a, 4, 1);
    CHECK(r4p_enc_take(&a, 4) == -1 && r4p_enc_take(&a, 3) == 0);
    for (i = 0; i < 100; i++)
        r4p_enc_add(&a, 0, 1);
    CHECK(r4p_enc_take(&a, 0) == 63);                                   /* clamped, the rest carries to the next frame */
    CHECK(r4p_enc_take(&a, 0) == 37 && r4p_enc_take(&a, 0) == 0);
    for (i = 0; i < 100; i++)
        r4p_enc_add(&a, 1, -1);
    CHECK(r4p_enc_take(&a, 1) == -63 && r4p_enc_take(&a, 1) == -37);
    r4p_enc_add(&a, 9, 1); CHECK(r4p_enc_take(&a, 9) == 0);             /* unknown encoder ignored */
}

static void t_leds(void)
{
    r4p_msg_t m, b;
    uint8_t pk[R4P_LED_BYTES];
    int i;
    for (i = 0; i < R4P_NBTN; i++) m.u.leds.btn[i] = (uint8_t)(i & 3);
    for (i = 0; i < R4P_NKEY; i++) m.u.leds.key[i] = (uint8_t)((i * 5 + 1) & 3);
    m.cmd = R4P_LEDS;
    CHECK(round_trip(&m, &b) == 1);
    CHECK(!memcmp(b.u.leds.btn, m.u.leds.btn, R4P_NBTN) && !memcmp(b.u.leds.key, m.u.leds.key, R4P_NKEY));
    CHECK(enc(&m) == 5 + 14 + 1);
    memset(&m.u.leds, 0, sizeof m.u.leds);
    m.u.leds.btn[0] = 1; m.u.leds.btn[1] = 2; m.u.leds.btn[2] = 3;      /* byte 0 = 1 | 2<<2 | 3<<4 = 0x39, LSB first */
    r4p_leds_pack(&m.u.leds, pk);
    CHECK(pk[0] == 0x39 && pk[1] == 0);
    m.u.leds.key[26] = 3;                                               /* entry 40: byte 13 bit 2..3 */
    r4p_leds_pack(&m.u.leds, pk);
    CHECK(pk[13] == 0x0C);
    for (i = 0; i < R4P_LED_BYTES; i++) CHECK(pk[i] < 0x80);
}

static void t_v11(void)
{
    r4p_msg_t m, b;
    r4p_list_t *l = &m.u.list;
    static const uint8_t V10_HELLO[] = {0xF0, 0x7D, 0x52, 0x34, 0x01, 0x01, 0x00, 0x01, 0x00, 0xF7};     /* a 1.0 device */
    static const uint8_t V10_ACK[] = {0xF0, 0x7D, 0x52, 0x34, 0x41, 0x01, 0xF7};                          /* a 1.0 host */
    size_t n;
    int i;
    /* the minor byte is optional in both directions: 1.0 frames still parse, with minor 0 */
    CHECK(r4p_parse(V10_HELLO, sizeof V10_HELLO, &b) == 1 && b.u.hello.minor == 0 && b.u.hello.fw_minor == 1);
    CHECK(r4p_parse(V10_ACK, sizeof V10_ACK, &b) == 1 && b.u.ack.proto == 1 && b.u.ack.minor == 0);
    n = r4p_hello(buf, sizeof buf, 0, 2, R4P_MODE_RUN4);
    CHECK(n == 11 && buf[9] == R4P_MINOR && buf[10] == 0xF7);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.hello.minor == R4P_MINOR);
    m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 1; m.u.ack.minor = 1;
    CHECK(enc(&m) == 8 && round_trip(&m, &b) == 1 && b.u.ack.minor == 1);
    /* LIST */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_LIST;
    strcpy(l->title, "MODE"); l->r = 127; l->g = 70; l->b = 20;
    l->total = 5; l->first = 0; l->sel = 2; l->n = 5;
    strcpy(l->item[0], "TAPE"); strcpy(l->item[1], "POLY"); strcpy(l->item[2], "DISC"); strcpy(l->item[3], "PERFORM");
    strcpy(l->item[4], "T\xD3LF");
    CHECK(round_trip(&m, &b) == 1 && b.cmd == R4P_LIST && !strcmp(b.u.list.title, "MODE") && b.u.list.total == 5 &&
          b.u.list.sel == 2 && b.u.list.n == 5 && !strcmp(b.u.list.item[4], "T\xD3LF") && !strcmp(b.u.list.item[3], "PERFORM"));
    /* a scrolled window of a long list */
    l->total = 100; l->first = 40; l->sel = 43; l->n = R4P_LIST_ROWS;
    for (i = 0; i < R4P_LIST_ROWS; i++) { strcpy(l->item[i], "KIT 00"); l->item[i][5] = (char)('0' + i); }
    CHECK(round_trip(&m, &b) == 1 && b.u.list.first == 40 && b.u.list.sel == 43 && !strcmp(b.u.list.item[6], "KIT 06"));
    l->total = 0; l->first = 0; l->sel = 0; l->n = 0;                                 /* total 0 closes the list */
    CHECK(round_trip(&m, &b) == 1 && b.u.list.total == 0 && b.u.list.n == 0);
    /* refused: too many rows, window past the end, selection outside, total over 127 */
    l->total = 20; l->first = 0; l->sel = 0; l->n = R4P_LIST_ROWS + 1; CHECK(enc(&m) == 0);
    l->total = 5; l->first = 3; l->n = 3; CHECK(enc(&m) == 0);
    l->total = 5; l->first = 0; l->n = 5; l->sel = 5; CHECK(enc(&m) == 0);
    l->total = 128; l->n = 0; l->sel = 0; CHECK(enc(&m) == 0);
    /* a damaged LIST: parse refuses bad windows and truncated frames, and ones with trailing bytes */
    l->total = 3; l->first = 0; l->sel = 1; l->n = 3; strcpy(l->item[0], "A"); strcpy(l->item[1], "B"); strcpy(l->item[2], "C");
    n = enc(&m);
    CHECK(n > 0 && r4p_parse(buf, n, &b) == 1);
    CHECK(r4p_parse(buf, n - 1, &b) == -1);                                            /* the F7 gone */
    buf[n - 1] = 0x00; buf[n] = 0xF7; CHECK(r4p_parse(buf, n + 1, &b) == -1);          /* trailing byte */
    buf[n - 1] = 0xF7;
    buf[5 + 5 + 3 + 2] = 9; CHECK(r4p_parse(buf, n, &b) == -1);                        /* sel (here byte 15) beyond total */
    /* LIST_PICK */
    n = r4p_list_pick(buf, sizeof buf, 7, R4P_PICK_OK);
    CHECK(n == 8 && buf[4] == 0x06 && buf[5] == 7 && buf[6] == 0);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.cmd == R4P_LIST_PICK && b.u.pick.index == 7 && b.u.pick.action == R4P_PICK_OK);
    CHECK(r4p_list_pick(buf, sizeof buf, 3, R4P_PICK_CANCEL) == 8 && r4p_list_pick(buf, sizeof buf, 3, R4P_PICK_MOVE) == 8);
    CHECK(r4p_list_pick(buf, sizeof buf, 3, 3) == 0 && r4p_list_pick(buf, sizeof buf, 128, 0) == 0);
    /* the longest LIST frame fits the device's buffer */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_LIST; l->total = 127; l->first = 100; l->sel = 100; l->n = R4P_LIST_ROWS;
    for (i = 0; i < 24; i++) l->title[i] = '\xD3';
    for (i = 0; i < R4P_LIST_ROWS; i++) { int k; for (k = 0; k < 24; k++) l->item[i][k] = '\xE9'; }
    n = enc(&m);
    CHECK(n > 0 && n <= 520 && n < R4P_FRAME_MAX);
    printf("  max LIST frame: %u bytes\n", (unsigned)n);
}

/* protocol 1.2: the PAGE trailer (cursor, position, path), FOCUS, VALUE flags; 1.1 frames unchanged */
static void t_v12(void)
{
    r4p_msg_t m, b;
    size_t n, n11, i;
    uint8_t f[700];
    m.cmd = R4P_PAGE; fill_page(&m.u.page);
    n11 = enc(&m);
    CHECK(r4p_parse(buf, n11, &b) == 1 && !b.u.page.v12 && b.u.page.cursor == R4P_CURSOR_NONE && b.u.page.item_count == 0 &&
          b.u.page.path[0] == 0);
    m.u.page.v12 = 1; m.u.page.cursor = 2; m.u.page.item_idx = 4; m.u.page.item_count = 12;
    strcpy(m.u.page.path, "TAPE > WEAR");
    n = enc(&m);
    CHECK(n == n11 + 1 + 2 + 2 + 1 + 11);
    /* the trailer's bytes: cursor, idx14, count14, path */
    CHECK(buf[n11 - 1] == 2 && buf[n11] == 0 && buf[n11 + 1] == 4 && buf[n11 + 2] == 0 && buf[n11 + 3] == 12 && buf[n11 + 4] == 11 &&
          buf[n11 + 5] == 'T');
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.page.v12 && b.u.page.cursor == 2 && b.u.page.item_idx == 4 && b.u.page.item_count == 12 &&
          !strcmp(b.u.page.path, "TAPE > WEAR") && !strcmp(b.u.page.slot[3].text, m.u.page.slot[3].text));
    memcpy(f, buf, n);
    for (i = n11 + 1; i < n - 1; i++) {             /* a partial trailer is refused (a cut right at its start is a 1.1 page) */
        uint8_t t[700];
        memcpy(t, f, i); t[i] = 0xF7;
        CHECK(r4p_parse(t, i + 1, &b) == -1);
    }
    m.u.page.cursor = R4P_CURSOR_NONE; m.u.page.item_idx = 0; m.u.page.item_count = 0; m.u.page.path[0] = 0;
    CHECK(round_trip(&m, &b) == 1 && b.u.page.v12 && b.u.page.cursor == R4P_CURSOR_NONE && b.u.page.item_count == 0);
    m.u.page.cursor = 4; CHECK(enc(&m) == 0);                        /* cursor 0..3 or 127 */
    m.u.page.cursor = 0; m.u.page.item_idx = 12; m.u.page.item_count = 12; CHECK(enc(&m) == 0);    /* idx < count */
    m.u.page.item_idx = 1; m.u.page.item_count = 0; CHECK(enc(&m) == 0);                          /* no count: idx 0 */
    m.u.page.item_idx = 16383; m.u.page.item_count = 16383; CHECK(enc(&m) == 0);
    m.u.page.item_idx = 16382; CHECK(round_trip(&m, &b) == 1 && b.u.page.item_idx == 16382 && b.u.page.item_count == 16383);
    f[n11 - 1] = 5; CHECK(r4p_parse(f, n, &b) == -1);               /* the parser checks the same */
    f[n11 - 1] = 2; f[n11 + 1] = 12; CHECK(r4p_parse(f, n, &b) == -1);
    f[n11 + 1] = 4; CHECK(r4p_parse(f, n, &b) == 1);
    /* the longest PAGE: every string 24 escaped characters, the path too */
    {
        int k, j;
        memset(&m.u.page, 0, sizeof m.u.page);
        for (k = 0; k < 24; k++) m.u.page.title[k] = m.u.page.track[k] = m.u.page.path[k] = '\xD3';
        for (j = 0; j < 4; j++) {
            for (k = 0; k < 24; k++) m.u.page.slot[j].name[k] = m.u.page.slot[j].text[k] = '\xE9';
            m.u.page.slot[j].kind = 4; m.u.page.slot[j].value = 16383;
        }
        m.u.page.r = m.u.page.g = m.u.page.b = 127;
        m.u.page.v12 = 1; m.u.page.cursor = 3; m.u.page.item_idx = 16382; m.u.page.item_count = 16383;
        n = enc(&m);
        printf("  max PAGE frame 1.2: %zu bytes (%zu between F0 and F7; limit %d)\n", n, n - 2, R4P_FRAME_MAX);
        CHECK(n == 569 && n - 2 < R4P_FRAME_MAX && r4p_parse(buf, n, &b) == 1 && (unsigned char)b.u.page.path[23] == 0xD3);
    }
    /* FOCUS */
    m.cmd = R4P_FOCUS; m.u.focus.cursor = 1; m.u.focus.item_idx = 7; m.u.focus.item_count = 9;
    n = enc(&m);
    CHECK(n == 11 && buf[4] == 0x48 && buf[5] == 1 && buf[6] == 0 && buf[7] == 7 && buf[8] == 0 && buf[9] == 9);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.cmd == R4P_FOCUS && b.u.focus.cursor == 1 && b.u.focus.item_idx == 7 && b.u.focus.item_count == 9);
    m.u.focus.cursor = R4P_CURSOR_NONE; m.u.focus.item_idx = 0; m.u.focus.item_count = 0;
    CHECK(round_trip(&m, &b) == 1 && b.u.focus.cursor == R4P_CURSOR_NONE);
    m.u.focus.cursor = 9; CHECK(enc(&m) == 0);
    m.u.focus.cursor = 0; m.u.focus.item_idx = 3; m.u.focus.item_count = 3; CHECK(enc(&m) == 0);
    {
        uint8_t g[] = {0xF0, 0x7D, 0x52, 0x34, R4P_FOCUS, 0, 0, 1, 0, 2, 0xF7}, h[] = {0xF0, 0x7D, 0x52, 0x34, R4P_FOCUS, 0, 0, 1, 0xF7};
        CHECK(r4p_parse(g, sizeof g, &b) == 1 && b.u.focus.item_idx == 1 && b.u.focus.item_count == 2);
        CHECK(r4p_parse(h, sizeof h, &b) == -1);                    /* short */
        g[5] = 127; CHECK(r4p_parse(g, sizeof g, &b) == 1);
        g[5] = 126; CHECK(r4p_parse(g, sizeof g, &b) == -1);
    }
    /* VALUE flags: absent = 0 (a 1.1 VALUE), bit 0 = big, other bits refused */
    m.cmd = R4P_VALUE; m.u.value.slot = 1; m.u.value.value = 100; strcpy(m.u.value.text, "0.40"); m.u.value.flags = 0;
    n11 = enc(&m);
    CHECK(r4p_parse(buf, n11, &b) == 1 && b.u.value.flags == 0);
    m.u.value.flags = R4P_VF_BIG;
    n = enc(&m);
    CHECK(n == n11 + 1 && buf[n - 2] == 1 && r4p_parse(buf, n, &b) == 1 && b.u.value.flags == R4P_VF_BIG && !strcmp(b.u.value.text, "0.40"));
    m.u.value.flags = 2; CHECK(enc(&m) == 0);
    buf[n - 2] = 2; CHECK(r4p_parse(buf, n, &b) == -1);
    /* HELLO says minor 2 */
    n = r4p_hello(buf, sizeof buf, 0, 4, R4P_MODE_RUN4);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.hello.minor == 2);
}

int main(void)
{
    t_simple();
    t_page_value_status_toast();
    t_strings();
    t_max_page();
    t_malformed();
    t_enc_acc();
    t_leds();
    t_v11();
    t_v12();
    printf("r4p: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
