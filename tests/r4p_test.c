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
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.hello.minor >= 2);
}

/* protocol 1.3: the host voice, the voice list, the tape view, the backup */
#define DEV_MAX 192            /* a device->host frame must fit the Bluetooth queue whole: 64 packets of 3 bytes */
static void t_v13(void)
{
    r4p_msg_t m, b;
    size_t n;
    int i;
    /* HELLO says the firmware's minor (3 when 1.3 came; 4 since 1.4) */
    n = r4p_hello(buf, sizeof buf, 0, 5, R4P_MODE_RUN4);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.hello.minor == R4P_MINOR && b.u.hello.fw_minor == 5);
    /* CRC-32: the IEEE check value, chained as zlib does */
    CHECK(r4p_crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u);
    CHECK(r4p_crc32(r4p_crc32(0, (const uint8_t *)"1234", 4), (const uint8_t *)"56789", 5) == 0xCBF43926u);
    /* Protocol 1.8 audio lease: strict boolean, no extra payload. */
    memset(&m, 0, sizeof m); m.cmd = R4P_AUDIO_ROUTE; m.u.b1.v = 1;
    n = enc(&m);
    CHECK(n == 7 && buf[4] == 0x5F && buf[5] == 1);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.b1.v == 1);
    m.u.b1.v = 2; CHECK(enc(&m) == 0);
    buf[5] = 2; CHECK(r4p_parse(buf, n, &b) == -1);

    /* HV_ON */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_HV_ON; m.u.hvon.on = 1; m.u.hvon.chan = 15;
    n = enc(&m);
    CHECK(n == 8 && buf[4] == 0x49 && buf[5] == 1 && buf[6] == 15);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.hvon.on == 1 && b.u.hvon.chan == 15);
    m.u.hvon.chan = 16; CHECK(enc(&m) == 0);
    m.u.hvon.chan = 0; m.u.hvon.on = 2; CHECK(enc(&m) == 0);
    /* HV_STATE */
    m.cmd = R4P_HV_STATE; m.u.hvstate.state = R4P_HV_SOUNDING; m.u.hvstate.chan = 15; m.u.hvstate.voice = 3;
    strcpy(m.u.hvstate.name, "E.PIANO 1");
    n = enc(&m);
    CHECK(n == 19 && r4p_parse(buf, n, &b) == 1 && b.u.hvstate.state == 1 && b.u.hvstate.voice == 3 && !strcmp(b.u.hvstate.name, "E.PIANO 1"));
    m.u.hvstate.voice = R4P_VOICE_HV; CHECK(round_trip(&m, &b) == 1 && b.u.hvstate.voice == 127);
    m.u.hvstate.voice = 40; CHECK(enc(&m) == 0);
    m.u.hvstate.voice = 0; m.u.hvstate.state = 3; CHECK(enc(&m) == 0);
    /* HV_VOICE: a stored one, 128 packed bytes, 155 VCED bytes */
    m.cmd = R4P_HV_VOICE; m.u.hvvoice.kind = R4P_HVV_STORED; m.u.hvvoice.idx = 39;
    n = enc(&m);
    CHECK(n == 8 && buf[5] == 0 && buf[6] == 39 && r4p_parse(buf, n, &b) == 1 && b.u.hvvoice.idx == 39);
    m.u.hvvoice.idx = 40; CHECK(enc(&m) == 0);
    m.u.hvvoice.kind = R4P_HVV_PACKED;
    for (i = 0; i < R4P_VCED; i++) m.u.hvvoice.data[i] = (uint8_t)(i & 0x7F);
    n = enc(&m);
    CHECK(n == 6 + 1 + 128 && r4p_parse(buf, n, &b) == 1 && !memcmp(b.u.hvvoice.data, m.u.hvvoice.data, 128));
    CHECK(r4p_parse_body(buf + 1, n - 3, &b) == -1);                 /* one byte short */
    m.u.hvvoice.kind = R4P_HVV_VCED;
    n = enc(&m);
    CHECK(n == 6 + 1 + 155 && r4p_parse(buf, n, &b) == 1 && b.u.hvvoice.kind == 2 && !memcmp(b.u.hvvoice.data, m.u.hvvoice.data, 155));
    m.u.hvvoice.data[7] = 0x80; CHECK(enc(&m) == 0);                 /* 7-bit only */
    m.u.hvvoice.kind = 3; CHECK(enc(&m) == 0);
    /* HV_PARAM: pairs, each in its range */
    m.cmd = R4P_HV_PARAM; m.u.hvparam.n = 3;
    m.u.hvparam.id[0] = R4P_HVP_ALG; m.u.hvparam.val[0] = 31;
    m.u.hvparam.id[1] = R4P_HVP_BRIGHT; m.u.hvparam.val[1] = 127;
    m.u.hvparam.id[2] = R4P_HVP_DLYTIME; m.u.hvparam.val[2] = 5;
    n = enc(&m);
    CHECK(n == 12 && r4p_parse(buf, n, &b) == 1 && b.u.hvparam.n == 3 && b.u.hvparam.id[2] == R4P_HVP_DLYTIME && b.u.hvparam.val[1] == 127);
    m.u.hvparam.val[0] = 32; CHECK(enc(&m) == 0);                     /* ALGORITHM 0..31 */
    m.u.hvparam.val[0] = 0; m.u.hvparam.id[0] = R4P_HVP_COUNT; CHECK(enc(&m) == 0);
    m.u.hvparam.id[0] = 0; m.u.hvparam.n = 0; CHECK(enc(&m) == 0);
    m.u.hvparam.n = R4P_HVP_COUNT;
    for (i = 0; i < R4P_HVP_COUNT; i++) { m.u.hvparam.id[i] = (uint8_t)i; m.u.hvparam.val[i] = R4P_HVP_MAX[i]; }
    n = enc(&m);
    CHECK(n == 6 + 2 * R4P_HVP_COUNT && r4p_parse(buf, n, &b) == 1 && b.u.hvparam.n == R4P_HVP_COUNT);
    {
        uint8_t odd[] = {0xF0, 0x7D, 0x52, 0x34, R4P_HV_PARAM, 0, 3, 1, 0xF7}, none[] = {0xF0, 0x7D, 0x52, 0x34, R4P_HV_PARAM, 0xF7};
        uint8_t over[] = {0xF0, 0x7D, 0x52, 0x34, R4P_HV_PARAM, 1, 8, 0xF7};
        CHECK(r4p_parse(odd, sizeof odd, &b) == -1 && r4p_parse(none, sizeof none, &b) == -1 && r4p_parse(over, sizeof over, &b) == -1);
    }
    {   /* 18..20, the morph (firmware 0.6): SET 0..2, MODE 0..11, MORPH 0..100; 21 is no id */
        uint8_t mo[] = {0xF0, 0x7D, 0x52, 0x34, R4P_HV_PARAM, 18, 1, 19, 11, 20, 100, 0xF7};
        uint8_t mo_set3[] = {0xF0, 0x7D, 0x52, 0x34, R4P_HV_PARAM, 18, 3, 0xF7}, mo_mode12[] = {0xF0, 0x7D, 0x52, 0x34, R4P_HV_PARAM, 19, 12, 0xF7};
        uint8_t mo_101[] = {0xF0, 0x7D, 0x52, 0x34, R4P_HV_PARAM, 20, 101, 0xF7}, id21[] = {0xF0, 0x7D, 0x52, 0x34, R4P_HV_PARAM, 21, 0, 0xF7};
        CHECK(R4P_HVP_COUNT == 21 && R4P_HVP_MORPH == 20);
        CHECK(r4p_parse(mo, sizeof mo, &b) == 1 && b.u.hvparam.n == 3 && b.u.hvparam.id[1] == R4P_HVP_MORPHMODE && b.u.hvparam.val[2] == 100);
        CHECK(r4p_parse(mo_set3, sizeof mo_set3, &b) == -1 && r4p_parse(mo_mode12, sizeof mo_mode12, &b) == -1);
        CHECK(r4p_parse(mo_101, sizeof mo_101, &b) == -1 && r4p_parse(id21, sizeof id21, &b) == -1);
    }
    /* NAMES_REQ, VOICE_GET */
    m.cmd = R4P_NAMES_REQ; n = enc(&m);
    CHECK(n == 6 && buf[4] == 0x4C && r4p_parse(buf, n, &b) == 1 && b.cmd == R4P_NAMES_REQ);
    m.cmd = R4P_VOICE_GET; m.u.b1.v = R4P_VOICE_HV; CHECK(round_trip(&m, &b) == 1 && b.u.b1.v == 127);
    m.u.b1.v = 41; CHECK(enc(&m) == 0);
    /* VOICE_NAMES: 8 at most, inside the total; names 10 characters */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_VOICE_NAMES; m.u.names.first = 32; m.u.names.total = 40; m.u.names.n = 8;
    for (i = 0; i < 8; i++) { m.u.names.used[i] = (uint8_t)(i & 1); strcpy(m.u.names.name[i], i & 1 ? "BRASS 1234" : ""); }
    n = enc(&m);
    printf("  VOICE_NAMES 8: %zu bytes\n", n);
    CHECK(n <= DEV_MAX && r4p_parse(buf, n, &b) == 1 && b.u.names.first == 32 && b.u.names.n == 8 && b.u.names.used[7] == 1 &&
          !strcmp(b.u.names.name[7], "BRASS 1234") && b.u.names.name[6][0] == 0);
    m.u.names.first = 33; CHECK(enc(&m) == 0);                        /* past the total */
    m.u.names.first = 0; m.u.names.n = 9; CHECK(enc(&m) == 0);
    m.u.names.n = 0; CHECK(enc(&m) == 0);
    m.u.names.n = 1; memcpy(m.u.names.name[0], "ELEVEN CHAR", 11); CHECK(enc(&m) == 0);
    /* VOICE_DATA: the data only when used */
    m.cmd = R4P_VOICE_DATA; m.u.voice.idx = 9; m.u.voice.used = 1;
    for (i = 0; i < 128; i++) m.u.voice.data[i] = (uint8_t)(127 - i);
    n = enc(&m);
    CHECK(n == 8 + 128 && n <= DEV_MAX && r4p_parse(buf, n, &b) == 1 && b.u.voice.idx == 9 && !memcmp(b.u.voice.data, m.u.voice.data, 128));
    m.u.voice.used = 0; n = enc(&m);
    CHECK(n == 8 && r4p_parse(buf, n, &b) == 1 && b.u.voice.used == 0 && b.u.voice.data[0] == 0);
    m.u.voice.idx = 40; CHECK(enc(&m) == 0);
    /* BK_PART / BK_DATA: always 128 bytes; parts 0..31 */
    m.cmd = R4P_BK_PART; m.u.voice.idx = 31; m.u.voice.used = 0;
    n = enc(&m);
    CHECK(n == 8 + 128 && n <= DEV_MAX && buf[4] == 0x0A && r4p_parse(buf, n, &b) == 1 && b.cmd == R4P_BK_PART && b.u.voice.idx == 31);
    m.cmd = R4P_BK_DATA; n = enc(&m);
    CHECK(n == 8 + 128 && buf[4] == 0x53 && r4p_parse(buf, n, &b) == 1 && b.cmd == R4P_BK_DATA);
    m.u.voice.idx = 32; CHECK(enc(&m) == 0);
    /* BK_END / BK_INFO: the CRC as five 7-bit groups, the lowest first */
    m.cmd = R4P_BK_END; m.u.bksum.count = 32; m.u.bksum.crc = 0xCBF43926u;
    n = enc(&m);
    CHECK(n == 12 && buf[5] == 32 && buf[6] == (0xCBF43926u & 0x7F) && buf[10] == (0xCBF43926u >> 28));
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.bksum.crc == 0xCBF43926u && b.u.bksum.count == 32);
    buf[10] = 0x10; CHECK(r4p_parse(buf, n, &b) == -1);              /* bit 32 */
    m.cmd = R4P_BK_INFO; m.u.bksum.count = 0; m.u.bksum.crc = 0; CHECK(round_trip(&m, &b) == 1 && b.cmd == R4P_BK_INFO && !b.u.bksum.count);
    m.u.bksum.count = 33; CHECK(enc(&m) == 0);
    m.cmd = R4P_BK_GET; m.u.b1.v = R4P_BK_SUMMARY; CHECK(round_trip(&m, &b) == 1 && b.u.b1.v == 127);
    m.u.b1.v = 31; CHECK(round_trip(&m, &b) == 1);
    m.u.b1.v = 32; CHECK(enc(&m) == 0);
    m.cmd = R4P_BK_ACK; m.u.b1.v = R4P_BK_BAD; CHECK(round_trip(&m, &b) == 1 && b.u.b1.v == 1);
    m.u.b1.v = 3; CHECK(enc(&m) == 0);
    /* the CRC of a backup part covers k, used and the data */
    {
        r4p_voice_t p;
        uint8_t raw[130];
        memset(&p, 0, sizeof p);
        p.idx = 5; p.used = 1;
        for (i = 0; i < 128; i++) p.data[i] = (uint8_t)(i * 3 & 0x7F);
        raw[0] = 5; raw[1] = 1; memcpy(raw + 2, p.data, 128);
        CHECK(r4p_crc32_part(0x1234u, &p) == r4p_crc32(0x1234u, raw, 130));
    }
    /* TAPE_WAVE: 0..120 columns, track 0..3 */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_TAPE_WAVE; m.u.wave.track = 0; strcpy(m.u.wave.label, "T1  LOOP 2  4 BARS"); m.u.wave.n = 120;
    for (i = 0; i < 120; i++) m.u.wave.peak[i] = (uint8_t)(i & 0x7F);
    n = enc(&m);
    printf("  TAPE_WAVE 120: %zu bytes\n", n);
    CHECK(n == 6 + 1 + 19 + 1 + 120 && r4p_parse(buf, n, &b) == 1 && b.u.wave.n == 120 && b.u.wave.peak[119] == 119 &&
          !strcmp(b.u.wave.label, "T1  LOOP 2  4 BARS"));
    m.u.wave.n = 121; CHECK(enc(&m) == 0);
    m.u.wave.n = 0; CHECK(round_trip(&m, &b) == 1 && b.u.wave.n == 0);
    m.u.wave.track = 4; CHECK(enc(&m) == 0);
    /* TAPE_POS: in <= out unless NOLOOP */
    m.cmd = R4P_TAPE_POS; m.u.tpos.play = 9000; m.u.tpos.in = 4096; m.u.tpos.out = 12288; m.u.tpos.flags = R4P_TP_PLAY;
    n = enc(&m);
    CHECK(n == 13 && r4p_parse(buf, n, &b) == 1 && b.u.tpos.play == 9000 && b.u.tpos.out == 12288 && b.u.tpos.flags == 1);
    m.u.tpos.in = 13000; CHECK(enc(&m) == 0);
    m.u.tpos.flags = R4P_TP_NOLOOP; CHECK(round_trip(&m, &b) == 1);
    m.u.tpos.flags = 8; CHECK(enc(&m) == 0);
    /* METERS */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_METERS; m.u.meters.level[0] = 127; m.u.meters.level[4] = 64; m.u.meters.clip = 0x11;
    n = enc(&m);
    CHECK(n == 12 && r4p_parse(buf, n, &b) == 1 && b.u.meters.level[0] == 127 && b.u.meters.level[4] == 64 && b.u.meters.clip == 0x11);
    m.u.meters.clip = 32; CHECK(enc(&m) == 0);
    {
        uint8_t shortm[] = {0xF0, 0x7D, 0x52, 0x34, R4P_METERS, 1, 2, 3, 4, 5, 0xF7};
        CHECK(r4p_parse(shortm, sizeof shortm, &b) == -1);
    }
}

/* protocol 1.4: the local modes */
static void fill_dparam(r4p_dparam_t *d)
{
    memset(d, 0, sizeof *d);
    d->id = 300; d->page = 2; d->kind = R4P_LK_NUMBER; d->flags = R4P_PF_BIPOLAR; d->step = 1;
    d->lo = -50; d->hi = 50; d->def = 0; d->value = -7;
    strcpy(d->name, "TUNE"); strcpy(d->unit, "ct"); strcpy(d->text, "-7 CT");
}
static void t_v14(void)
{
    r4p_msg_t m, b;
    size_t n;
    int i;
    uint32_t h1, h2;
    CHECK(R4P_MINOR >= 4);
    /* MODES: three, AMB absent in a build without it */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_MODES; m.u.modes.cur = R4P_MID_AMB; m.u.modes.n = 3;
    for (i = 0; i < 3; i++) {
        static const char *const NM[3] = {"RUN4", "SYNTH", "AMB"};
        m.u.modes.m[i].id = (uint8_t)i; strcpy(m.u.modes.m[i].name, NM[i]);
        m.u.modes.m[i].flags = (uint8_t)(R4P_MF_AVAIL | (i ? R4P_MF_DESC : R4P_MF_CTRL));
    }
    n = enc(&m);
    CHECK(n == 5 + 2 + 3 * 3 + (4 + 5 + 3) + 1);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.modes.cur == 2 && b.u.modes.n == 3 && !strcmp(b.u.modes.m[1].name, "SYNTH") &&
          b.u.modes.m[0].flags == (R4P_MF_AVAIL | R4P_MF_CTRL));
    m.u.modes.n = 0; CHECK(enc(&m) == 0);
    m.u.modes.n = 9; CHECK(enc(&m) == 0);
    m.u.modes.n = 3; m.u.modes.m[2].flags = 8; CHECK(enc(&m) == 0);
    m.u.modes.m[2].flags = 0; m.u.modes.cur = 127; CHECK(enc(&m) == 0);
    /* MODE and DESC_END: the hash in five 7-bit groups */
    m.cmd = R4P_MODE; m.u.mode.mode = 2; m.u.mode.flags = 3; m.u.mode.hash = 0xCBF43926u;
    n = enc(&m);
    CHECK(n == 13 && buf[5] == 2 && buf[6] == 3 && buf[7] == 0x26 && buf[11] == 0x0C);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.mode.hash == 0xCBF43926u && b.u.mode.flags == 3);
    m.cmd = R4P_DESC_END; n = enc(&m);
    CHECK(n == 12 && r4p_parse(buf, n, &b) == 1 && b.cmd == R4P_DESC_END && b.u.mode.hash == 0xCBF43926u);
    /* DESC_HEAD */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_DESC_HEAD; m.u.dhead.mode = 2; m.u.dhead.hash = 0x12345678u; m.u.dhead.npages = 8; m.u.dhead.nparams = 35;
    m.u.dhead.nstate = 4; strcpy(m.u.dhead.title, "AMB");
    strcpy(m.u.dhead.state[0], "PIECE"); strcpy(m.u.dhead.state[1], "CHORD"); strcpy(m.u.dhead.state[2], "SCALE");
    strcpy(m.u.dhead.state[3], "STATE");
    CHECK(round_trip(&m, &b) == 1 && b.u.dhead.nparams == 35 && b.u.dhead.hash == 0x12345678u && !strcmp(b.u.dhead.state[3], "STATE"));
    m.u.dhead.nstate = 5; CHECK(enc(&m) == 0);
    m.u.dhead.nstate = 4; m.u.dhead.npages = 33; CHECK(enc(&m) == 0);
    m.u.dhead.npages = 8; m.u.dhead.nparams = 1025; CHECK(enc(&m) == 0);
    /* DESC_PAGE */
    m.cmd = R4P_DESC_PAGE; m.u.dpage.page = 31; strcpy(m.u.dpage.title, "SPACE");
    CHECK(round_trip(&m, &b) == 1 && b.u.dpage.page == 31 && !strcmp(b.u.dpage.title, "SPACE"));
    m.u.dpage.page = 32; CHECK(enc(&m) == 0);
    /* DESC_PARAM: signed values, the rules per kind */
    m.cmd = R4P_DESC_PARAM; fill_dparam(&m.u.dparam);
    n = enc(&m);
    CHECK(n == 6 + 13 + 5 + 3 + 2 + 6);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.dparam.lo == -50 && b.u.dparam.hi == 50 && b.u.dparam.value == -7 &&
          b.u.dparam.id == 300 && !strcmp(b.u.dparam.unit, "ct") && !strcmp(b.u.dparam.text, "-7 CT"));
    CHECK(buf[11] == ((-50 + 8192) >> 7) && buf[12] == ((-50 + 8192) & 0x7F));      /* lo = v + 8192 */
    m.u.dparam.value = 51; CHECK(enc(&m) == 0);                                       /* outside lo..hi */
    m.u.dparam.value = 0; m.u.dparam.def = -51; CHECK(enc(&m) == 0);
    m.u.dparam.def = 0; m.u.dparam.step = 0; CHECK(enc(&m) == 0);
    m.u.dparam.step = 1; m.u.dparam.flags = 8; CHECK(enc(&m) == 0);
    m.u.dparam.flags = 0; m.u.dparam.lo = -9000; CHECK(enc(&m) == 0);                 /* beyond 14 bits signed */
    m.u.dparam.lo = 0; m.u.dparam.hi = 2; m.u.dparam.kind = R4P_LK_ENUM; m.u.dparam.nlabels = 3; CHECK(round_trip(&m, &b) == 1);
    m.u.dparam.nlabels = 2; CHECK(enc(&m) == 0);                                       /* an enum: a label per value */
    m.u.dparam.kind = R4P_LK_MASK; m.u.dparam.hi = 4095; m.u.dparam.nlabels = 12; CHECK(round_trip(&m, &b) == 1 && b.u.dparam.hi == 4095);
    m.u.dparam.hi = 4094; CHECK(enc(&m) == 0);
    m.u.dparam.kind = R4P_LK_TOGGLE; m.u.dparam.hi = 1; m.u.dparam.nlabels = 2; CHECK(round_trip(&m, &b) == 1);
    m.u.dparam.nlabels = 1; CHECK(enc(&m) == 0);
    m.u.dparam.kind = R4P_LK_ACTION; m.u.dparam.nlabels = 0; CHECK(round_trip(&m, &b) == 1);
    m.u.dparam.def = 1; CHECK(enc(&m) == 0);
    m.u.dparam.def = 0; m.u.dparam.kind = R4P_LK_COUNT; CHECK(enc(&m) == 0);
    {   /* the longest: every string at its maximum, every character escaped, is still a device frame (<= 136) */
        r4p_dparam_t *d = &m.u.dparam;
        fill_dparam(d);
        memset(d->name, 0xC9, R4P_LM_STR); d->name[R4P_LM_STR] = 0;
        memset(d->unit, 0xC9, R4P_LM_UNIT); d->unit[R4P_LM_UNIT] = 0;
        memset(d->text, 0xC9, R4P_LM_STR); d->text[R4P_LM_STR] = 0;
        n = enc(&m);
        CHECK(n > 0 && n <= R4P_DEV_MAX);
        printf("  DESC_PARAM longest: %zu bytes\n", n);
        CHECK(r4p_parse(buf, n, &b) == 1 && (unsigned char)b.u.dparam.name[15] == 0xC9);
    }
    /* the hash leaves out the value and its text, and nothing else */
    m.cmd = R4P_DESC_PARAM; fill_dparam(&m.u.dparam);
    n = enc(&m); h1 = r4p_desc_hash_add(R4P_FNV0, buf, n);
    m.u.dparam.value = 33; strcpy(m.u.dparam.text, "+33 CT");
    n = enc(&m); h2 = r4p_desc_hash_add(R4P_FNV0, buf, n);
    CHECK(h1 == h2 && h1 != R4P_FNV0);
    strcpy(m.u.dparam.unit, "c");
    n = enc(&m); CHECK(r4p_desc_hash_add(R4P_FNV0, buf, n) != h1);
    CHECK(r4p_fnv(R4P_FNV0, (const uint8_t *)"a", 1) == 0xE40C292Cu);                  /* FNV-1a's own check value */
    m.cmd = R4P_DESC_HEAD; memset(&m.u.dhead, 0, sizeof m.u.dhead); strcpy(m.u.dhead.title, "X"); m.u.dhead.hash = 5;
    n = enc(&m); h1 = r4p_desc_hash_add(R4P_FNV0, buf, n);
    m.u.dhead.hash = 77; n = enc(&m); CHECK(r4p_desc_hash_add(R4P_FNV0, buf, n) == h1);   /* (not its own hash) */
    CHECK(r4p_desc_hash_add(5u, buf, 3) == 5u);                                          /* not a frame: unchanged */
    /* DESC_LABELS: windows of at most eight */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_DESC_LABELS; m.u.dlabels.id = 32; m.u.dlabels.first = 0; m.u.dlabels.total = 8; m.u.dlabels.n = 8;
    for (i = 0; i < 8; i++) snprintf(m.u.dlabels.label[i], sizeof m.u.dlabels.label[i], "PIECE %d", i);
    n = enc(&m);
    CHECK(n > 0 && n <= R4P_DEV_MAX && r4p_parse(buf, n, &b) == 1 && !strcmp(b.u.dlabels.label[7], "PIECE 7"));
    m.u.dlabels.first = 1; CHECK(enc(&m) == 0);                                        /* past total */
    m.u.dlabels.first = 0; m.u.dlabels.n = 0; CHECK(enc(&m) == 0);
    m.u.dlabels.n = 8;
    for (i = 0; i < 8; i++) { memset(m.u.dlabels.label[i], 0xE9, 16); m.u.dlabels.label[i][16] = 0; }
    CHECK(enc(&m) == 0);                                                               /* too long for a device frame */
    m.u.dlabels.n = 3; m.u.dlabels.total = 3; CHECK(enc(&m) > 0);
    /* PVAL, PSET, LSTATE, LM_ERR */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_PVAL; m.u.pval.mode = 2; m.u.pval.n = R4P_PVAL_MAX;
    for (i = 0; i < R4P_PVAL_MAX; i++) {
        m.u.pval.id[i] = (uint16_t)(i * 1000); m.u.pval.v[i] = (int16_t)(i * 3000 - 8192);
        memset(m.u.pval.text[i], 'A' + i, 16); m.u.pval.text[i][16] = 0;
    }
    n = enc(&m);
    CHECK(n == 6 + 2 + 6 * (2 + 2 + 17) && n <= R4P_DEV_MAX);
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.pval.n == 6 && b.u.pval.v[0] == -8192 && b.u.pval.v[5] == 6808 && b.u.pval.id[5] == 5000);
    m.u.pval.n = 0; CHECK(enc(&m) == 0);
    m.u.pval.n = 7; CHECK(enc(&m) == 0);
    memset(&m, 0, sizeof m);
    m.cmd = R4P_PSET; m.u.pset.mode = 2; m.u.pset.n = R4P_PSET_MAX;
    for (i = 0; i < R4P_PSET_MAX; i++) { m.u.pset.id[i] = (uint16_t)i; m.u.pset.v[i] = (int16_t)(i - 8); }
    n = enc(&m);
    CHECK(n == 7 + 16 * 4 && r4p_parse(buf, n, &b) == 1 && b.u.pset.n == 16 && b.u.pset.v[0] == -8 && b.u.pset.id[15] == 15);
    m.u.pset.n = 17; CHECK(enc(&m) == 0);
    m.u.pset.n = 0; CHECK(enc(&m) == 0);
    {
        uint8_t odd[] = {0xF0, 0x7D, 0x52, 0x34, R4P_PSET, 2, 0, 1, 0x40, 0, 0, 0xF7};    /* a pair and a half */
        uint8_t none[] = {0xF0, 0x7D, 0x52, 0x34, R4P_PSET, 2, 0xF7};
        uint8_t big[7 + 17 * 4];
        CHECK(r4p_parse(odd, sizeof odd, &b) == -1);
        CHECK(r4p_parse(none, sizeof none, &b) == -1);
        memset(big, 0, sizeof big);
        big[0] = 0xF0; big[1] = 0x7D; big[2] = 0x52; big[3] = 0x34; big[4] = R4P_PSET; big[5] = 1; big[sizeof big - 1] = 0xF7;
        CHECK(r4p_parse(big, sizeof big, &b) == -1);                                     /* 17 pairs */
    }
    memset(&m, 0, sizeof m);
    m.cmd = R4P_LSTATE; m.u.lstate.mode = 2; m.u.lstate.flags = R4P_LS_PLAYING | R4P_LS_EXTCLOCK; m.u.lstate.n = 4;
    strcpy(m.u.lstate.text[0], "GARDEN"); strcpy(m.u.lstate.text[1], "Em7"); strcpy(m.u.lstate.text[2], "D DORIAN");
    strcpy(m.u.lstate.text[3], "GROWING");
    CHECK(round_trip(&m, &b) == 1 && b.u.lstate.flags == 5 && !strcmp(b.u.lstate.text[1], "Em7"));
    m.u.lstate.flags = 32; CHECK(enc(&m) == 0);
    m.u.lstate.flags = 0; m.u.lstate.n = 5; CHECK(enc(&m) == 0);
    m.u.lstate.n = 0; CHECK(round_trip(&m, &b) == 1 && b.u.lstate.n == 0);
    m.cmd = R4P_LM_ERR; m.u.lerr.mode = 1; m.u.lerr.code = R4P_LE_RANGE; m.u.lerr.id = 16383;
    CHECK(enc(&m) == 10 && round_trip(&m, &b) == 1 && b.u.lerr.code == R4P_LE_RANGE && b.u.lerr.id == 16383);
    m.u.lerr.code = 0; CHECK(enc(&m) == 0);
    m.u.lerr.code = R4P_LE_COUNT; CHECK(enc(&m) == 0);
    /* the small host frames */
    m.cmd = R4P_MODES_REQ; CHECK(enc(&m) == 6 && round_trip(&m, &b) == 1 && b.cmd == R4P_MODES_REQ);
    for (i = R4P_MODE_SET; i <= R4P_VALS_REQ; i++) {
        m.cmd = (uint8_t)i; m.u.b1.v = 2;
        CHECK(enc(&m) == 7 && round_trip(&m, &b) == 1 && b.cmd == i && b.u.b1.v == 2);
        m.u.b1.v = 127; CHECK(enc(&m) == 0);
    }
    {   /* a string longer than its field's maximum is refused, not cut, by the reader */
        uint8_t f[] = {0xF0, 0x7D, 0x52, 0x34, R4P_DESC_PAGE, 0, 17, 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A',
                       'A', 'A', 'A', 'A', 'A', 0xF7};
        CHECK(r4p_parse(f, sizeof f, &b) == -1);
        f[6] = 16;
        f[sizeof f - 2] = 0xF7;
        CHECK(r4p_parse(f, sizeof f - 1, &b) == 1 && strlen(b.u.dpage.title) == 16);
    }
    /* a 1.3 host's frames still parse alongside (nothing changed for them) */
    m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 1; m.u.ack.minor = 4; CHECK(enc(&m) == 8 && round_trip(&m, &b) == 1 && b.u.ack.minor == 4);
}

/* protocol 1.5: the local mode's page */
static void t_v15(void)
{
    r4p_msg_t m, b;
    size_t n;
    int i;
    static const uint8_t AMB_GARDEN[] = {0xF0, 0x7D, 0x52, 0x34, 0x17, 0x02, 0x01, 0x04, 0x00, 0x12, 0x00, 0x05, 0x00, 0x0F,
                                         0x00, 0x06, 0xF7};      /* AMB page 1 GARDEN: TEMPO 18, EVOLVE 5, MOTION 15, DRIFT 6 */
    static const uint8_t SY_OP3[] = {0xF0, 0x7D, 0x52, 0x34, 0x17, 0x01, 0x05, 0x04, 0x00, 0x30, 0x00, 0x31, 0x00, 0x32,
                                     0x00, 0x33, 0xF7};          /* Synth page 5 OP3, OPERATOR 1: ids 48..51 */
    static const uint8_t SY_GLO2[] = {0xF0, 0x7D, 0x52, 0x34, 0x17, 0x01, 0x0B, 0x02, 0x01, 0x04, 0x01, 0x05, 0xF7};
    static const uint8_t SY_SEQ[] = {0xF0, 0x7D, 0x52, 0x34, 0x17, 0x01, 0x0D, 0x00, 0xF7};
    static const uint8_t SET_BELLS[] = {0xF0, 0x7D, 0x52, 0x34, 0x59, 0x02, 0x03, 0xF7};
    CHECK(R4P_MINOR >= 5);
    n = r4p_hello(buf, sizeof buf, 0, 8, R4P_MODE_SYNTH);
    CHECK(n == 11 && buf[9] == R4P_MINOR && r4p_parse(buf, n, &b) == 1 && b.u.hello.minor == R4P_MINOR);
    /* LVIEW: the exact bytes */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_LVIEW; m.u.lview.mode = R4P_MID_AMB; m.u.lview.page = 1; m.u.lview.n = 4;
    m.u.lview.id[0] = 18; m.u.lview.id[1] = 5; m.u.lview.id[2] = 15; m.u.lview.id[3] = 6;
    n = enc(&m);
    CHECK(n == sizeof AMB_GARDEN && !memcmp(buf, AMB_GARDEN, n));
    CHECK(r4p_parse(AMB_GARDEN, sizeof AMB_GARDEN, &b) == 1 && b.cmd == R4P_LVIEW && b.u.lview.mode == 2 &&
          b.u.lview.page == 1 && b.u.lview.n == 4 && b.u.lview.id[0] == 18 && b.u.lview.id[3] == 6);
    m.u.lview.mode = R4P_MID_SYNTH; m.u.lview.page = 5;
    for (i = 0; i < 4; i++) m.u.lview.id[i] = (uint16_t)(48 + i);
    n = enc(&m);
    CHECK(n == sizeof SY_OP3 && !memcmp(buf, SY_OP3, n));
    m.u.lview.page = 11; m.u.lview.n = 2; m.u.lview.id[0] = 132; m.u.lview.id[1] = 133;
    n = enc(&m);
    CHECK(n == sizeof SY_GLO2 && !memcmp(buf, SY_GLO2, n));
    CHECK(r4p_parse(SY_GLO2, sizeof SY_GLO2, &b) == 1 && b.u.lview.n == 2 && b.u.lview.id[1] == 133 && b.u.lview.page == 11);
    m.u.lview.page = 13; m.u.lview.n = 0;
    n = enc(&m);
    CHECK(n == sizeof SY_SEQ && !memcmp(buf, SY_SEQ, n));
    CHECK(r4p_parse(SY_SEQ, sizeof SY_SEQ, &b) == 1 && b.u.lview.n == 0 && b.u.lview.page == 13);
    /* a knob without a parameter in the middle: 16383 (NONE) */
    m.u.lview.page = 11; m.u.lview.n = 3; m.u.lview.id[0] = 128; m.u.lview.id[1] = R4P_LVIEW_NONE; m.u.lview.id[2] = 130;
    n = enc(&m);
    CHECK(n == 6 + 3 + 6 && buf[10] == 0x7F && buf[11] == 0x7F);
    CHECK(round_trip(&m, &b) == 1 && b.u.lview.n == 3 && b.u.lview.id[1] == R4P_LVIEW_NONE && b.u.lview.id[2] == 130);
    /* the encoder's checks */
    m.u.lview.n = 5; CHECK(enc(&m) == 0);
    m.u.lview.n = 1; m.u.lview.page = 32; CHECK(enc(&m) == 0);
    m.u.lview.page = 31; CHECK(enc(&m) > 0);
    m.u.lview.mode = 127; CHECK(enc(&m) == 0);
    m.u.lview.mode = 1; m.u.lview.id[0] = 16384; CHECK(enc(&m) == 0);
    {   /* the reader's: a bad count, a length that does not match it, a page past 31 */
        uint8_t f[sizeof AMB_GARDEN + 2];
        memcpy(f, AMB_GARDEN, sizeof AMB_GARDEN);
        f[7] = 5; CHECK(r4p_parse(f, sizeof AMB_GARDEN, &b) == -1);                   /* n 5 */
        f[7] = 3; CHECK(r4p_parse(f, sizeof AMB_GARDEN, &b) == -1);                   /* n 3, four ids: trailing */
        f[7] = 4; f[6] = 32; CHECK(r4p_parse(f, sizeof AMB_GARDEN, &b) == -1);       /* page 32 */
        f[6] = 1; f[sizeof AMB_GARDEN - 2] = 0xF7;
        CHECK(r4p_parse(f, sizeof AMB_GARDEN - 1, &b) == -1);                         /* half an id */
        memcpy(f, AMB_GARDEN, sizeof AMB_GARDEN);
        f[sizeof AMB_GARDEN - 1] = 0x00; f[sizeof AMB_GARDEN] = 0x01; f[sizeof AMB_GARDEN + 1] = 0xF7;
        CHECK(r4p_parse(f, sizeof f, &b) == -1);                                      /* an id too many */
        memcpy(f, AMB_GARDEN, 8); f[8] = 0xF7;
        CHECK(r4p_parse(f, 9, &b) == -1);                                             /* n 4, no ids */
        f[5] = 0x7F; f[7] = 0; CHECK(r4p_parse(f, 9, &b) == -1);                      /* mode 127 */
    }
    /* LVIEW_SET */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_LVIEW_SET; m.u.lview.mode = R4P_MID_AMB; m.u.lview.page = 3;
    n = enc(&m);
    CHECK(n == sizeof SET_BELLS && !memcmp(buf, SET_BELLS, n));
    CHECK(r4p_parse(SET_BELLS, sizeof SET_BELLS, &b) == 1 && b.cmd == R4P_LVIEW_SET && b.u.lview.mode == 2 && b.u.lview.page == 3);
    m.u.lview.page = 127; CHECK(round_trip(&m, &b) == 1 && b.u.lview.page == 127);   /* (past npages: the device's RANGE) */
    m.u.lview.mode = 127; CHECK(enc(&m) == 0);
    {
        uint8_t shortf[] = {0xF0, 0x7D, 0x52, 0x34, 0x59, 0x02, 0xF7}, longf[] = {0xF0, 0x7D, 0x52, 0x34, 0x59, 0x02, 0x03, 0x00, 0xF7};
        uint8_t badmode[] = {0xF0, 0x7D, 0x52, 0x34, 0x59, 0x7F, 0x03, 0xF7};
        CHECK(r4p_parse(shortf, sizeof shortf, &b) == -1);
        CHECK(r4p_parse(longf, sizeof longf, &b) == -1);
        CHECK(r4p_parse(badmode, sizeof badmode, &b) == -1);
    }
    /* 16383 is never a described parameter's id (it is LVIEW's NONE) */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_DESC_PARAM; fill_dparam(&m.u.dparam);
    m.u.dparam.id = 16382; CHECK(enc(&m) > 0);
    m.u.dparam.id = R4P_LVIEW_NONE; CHECK(enc(&m) == 0);
    m.u.dparam.id = 16382; n = enc(&m); buf[5] = 0x7F; buf[6] = 0x7F; CHECK(r4p_parse(buf, n, &b) == -1);
    /* a 1.5 host's HELLO_ACK */
    m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 1; m.u.ack.minor = 5; CHECK(enc(&m) == 8 && round_trip(&m, &b) == 1 && b.u.ack.minor == 5);
}

/* protocol 1.6: the scene page (SCENES, SCENE_STATE, SCENE_PICK) */
static void t_v16(void)
{
    r4p_msg_t m, b;
    size_t n;
    int i;
    static const uint8_t PICK17[] = {0xF0, 0x7D, 0x52, 0x34, 0x1B, 0x11, 0xF7};
    static const uint8_t STATE[] = {0xF0, 0x7D, 0x52, 0x34, 0x5D, 0x00, 0x03, 0x7F, 0x7F, 0xF7};
    static const uint8_t SCN2[] = {0xF0, 0x7D, 0x52, 0x34, 0x5C, 0x00, 0x03, 0x00, 0x02, 0x04, 'V', 'E', 'R', 'S', 0x00, 0xF7};
    CHECK(R4P_MINOR >= 6 && R4P_SCENE_PICK == 0x1B && R4P_SCENES == 0x5C && R4P_SCENE_STATE == 0x5D);
    /* SCENE_PICK: the index 0..127, exact bytes */
    n = r4p_scene_pick(buf, sizeof buf, 17);
    CHECK(n == sizeof PICK17 && !memcmp(buf, PICK17, n));
    CHECK(r4p_parse(PICK17, sizeof PICK17, &b) == 1 && b.cmd == R4P_SCENE_PICK && b.u.b1.v == 17);
    CHECK(r4p_scene_pick(buf, sizeof buf, 127) > 0 && r4p_scene_pick(buf, sizeof buf, 128) == 0);
    {
        uint8_t none[] = {0xF0, 0x7D, 0x52, 0x34, 0x1B, 0xF7};
        uint8_t two[] = {0xF0, 0x7D, 0x52, 0x34, 0x1B, 0x01, 0x02, 0xF7};
        CHECK(r4p_parse(none, sizeof none, &b) == -1 && r4p_parse(two, sizeof two, &b) == -1);
    }
    /* SCENE_STATE: active 3, queued none (16383 = 7F 7F) */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_SCENE_STATE; m.u.scene_state.active = 3; m.u.scene_state.queued = R4P_SCENE_NONE;
    n = enc(&m);
    CHECK(n == sizeof STATE && !memcmp(buf, STATE, n));
    CHECK(r4p_parse(STATE, sizeof STATE, &b) == 1 && b.u.scene_state.active == 3 && b.u.scene_state.queued == R4P_SCENE_NONE);
    m.u.scene_state.active = 127; m.u.scene_state.queued = 0;
    CHECK(round_trip(&m, &b) == 1 && b.u.scene_state.active == 127 && b.u.scene_state.queued == 0);
    m.u.scene_state.active = 128; CHECK(enc(&m) == 0);                               /* neither a scene nor NONE */
    m.u.scene_state.active = 0; m.u.scene_state.queued = 200; CHECK(enc(&m) == 0);
    {
        uint8_t bad[] = {0xF0, 0x7D, 0x52, 0x34, 0x5D, 0x01, 0x00, 0x00, 0x00, 0xF7};   /* active 128 */
        uint8_t shortf[] = {0xF0, 0x7D, 0x52, 0x34, 0x5D, 0x00, 0x03, 0x7F, 0xF7};
        CHECK(r4p_parse(bad, sizeof bad, &b) == -1 && r4p_parse(shortf, sizeof shortf, &b) == -1);
    }
    /* SCENES: total 3, names 0..2 (VERS, "", rest of the frame is the name's own bytes) */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_SCENES; m.u.scenes.total = 3; m.u.scenes.first = 0; m.u.scenes.n = 2;
    strcpy(m.u.scenes.name[0], "VERS"); m.u.scenes.name[1][0] = 0;
    n = enc(&m);
    CHECK(n == sizeof SCN2 - 0 && !memcmp(buf, SCN2, n - 1));                      /* (SCN2's tail is checked below) */
    CHECK(r4p_parse(buf, n, &b) == 1 && b.u.scenes.total == 3 && b.u.scenes.n == 2 && !strcmp(b.u.scenes.name[0], "VERS") &&
          b.u.scenes.name[1][0] == 0);
    /* the longest: 128 scenes, a bank of 16 names of 8 characters, Latin-1 intact */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_SCENES; m.u.scenes.total = 128; m.u.scenes.first = 112; m.u.scenes.n = 16;
    for (i = 0; i < 16; i++) strcpy(m.u.scenes.name[i], "ABCDEFG\xD3");
    n = enc(&m);
    CHECK(n == 5 + 2 + 2 + 16 * (1 + 7 + 2) + 1 && r4p_parse(buf, n, &b) == 1 && b.u.scenes.first == 112 && b.u.scenes.n == 16 &&
          !strcmp(b.u.scenes.name[15], "ABCDEFG\xD3") && b.u.scenes.total == 128);
    /* the count alone (n 0) says how many there are; 0 means none */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_SCENES; m.u.scenes.total = 0;
    CHECK(round_trip(&m, &b) == 1 && b.u.scenes.total == 0 && b.u.scenes.n == 0);
    m.u.scenes.total = 128; CHECK(round_trip(&m, &b) == 1 && b.u.scenes.total == 128);
    /* refused: more than 128, a window past the count, 17 names, a name too long on the wire */
    m.u.scenes.total = 129; CHECK(enc(&m) == 0);
    m.u.scenes.total = 20; m.u.scenes.first = 16; m.u.scenes.n = 5; CHECK(enc(&m) == 0);
    m.u.scenes.total = 40; m.u.scenes.first = 0; m.u.scenes.n = 17; CHECK(enc(&m) == 0);
    {
        uint8_t longname[] = {0xF0, 0x7D, 0x52, 0x34, 0x5C, 0x00, 0x03, 0x00, 0x01, 0x09, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 0xF7};
        uint8_t trunc[] = {0xF0, 0x7D, 0x52, 0x34, 0x5C, 0x00, 0x03, 0x00, 0x02, 0x01, 'A', 0xF7};
        uint8_t over[] = {0xF0, 0x7D, 0x52, 0x34, 0x5C, 0x00, 0x03, 0x00, 0x04, 0xF7};
        CHECK(r4p_parse(longname, sizeof longname, &b) == -1 && r4p_parse(trunc, sizeof trunc, &b) == -1 &&
              r4p_parse(over, sizeof over, &b) == -1);
    }
    /* the 1.6 HELLO_ACK / HELLO carry minor 6 */
    m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 1; m.u.ack.minor = 6; CHECK(enc(&m) == 8 && round_trip(&m, &b) == 1 && b.u.ack.minor == 6);
}

/* protocol 1.7: the piano roll. The vectors are the app's too (scripts/run4ctl_protocol_test/v17_vectors.txt) */
static void roll_a(r4p_msg_t *m)
{
    memset(m, 0, sizeof *m);
    m->cmd = R4P_ROLL;
    m->u.roll.flags = R4P_RF_PLAY; m->u.roll.lo = 60; m->u.roll.rows = 12; m->u.roll.steps = 16;
    m->u.roll.first = 16; m->u.roll.total = 32; m->u.roll.cur_col = 2; m->u.roll.cur_row = 4; m->u.roll.play = 5;
    m->u.roll.beat = 4; strcpy(m->u.roll.label, "C4-B4"); m->u.roll.n = 3;
    m->u.roll.note[0] = (r4p_rnote_t){0, 2, 0, 100, R4P_RN_IN};
    m->u.roll.note[1] = (r4p_rnote_t){2, 1, 4, 127, R4P_RN_SEL};
    m->u.roll.note[2] = (r4p_rnote_t){14, 2, 11, 64, R4P_RN_OUT | R4P_RN_MUTED};
}
static void t_v17(void)
{
    r4p_msg_t m, b;
    size_t n;
    int i;
    static const uint8_t ROLL_A[] = {0xF0, 0x7D, 0x52, 0x34, 0x5E, 0x01, 0x3C, 0x0C, 0x10, 0x00, 0x10, 0x00, 0x20, 0x02, 0x04,
                                     0x05, 0x04, 0x05, 'C', '4', '-', 'B', '4', 0x03, 0x00, 0x02, 0x00, 0x64, 0x01, 0x02,
                                     0x01, 0x04, 0x7F, 0x08, 0x0E, 0x02, 0x0B, 0x40, 0x06, 0xF7};
    static const uint8_t ROLL_B[] = {0xF0, 0x7D, 0x52, 0x34, 0x5E, 0x03, 0x67, 0x18, 0x20, 0x02, 0x20, 0x02, 0x2C, 0x7F, 0x7F,
                                     0x1F, 0x00, 0x04, 'T', 0x7F, 0x53, 'L', 'F', 0x01, 0x1F, 0x01, 0x17, 0x00, 0x10, 0xF7};
    static const uint8_t ROLL_END[] = {0xF0, 0x7D, 0x52, 0x34, 0x5E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x7F,
                                       0x7F, 0x00, 0x00, 0x00, 0xF7};
    CHECK(R4P_MINOR >= 7 && R4P_ROLL == 0x5E);
    /* A: 12 rows from C4, steps 17..32 of 32, the cursor on a selected note, a note from before and one going on */
    roll_a(&m);
    n = enc(&m);
    CHECK(n == sizeof ROLL_A && !memcmp(buf, ROLL_A, n));
    CHECK(r4p_parse(ROLL_A, sizeof ROLL_A, &b) == 1 && b.cmd == R4P_ROLL && b.u.roll.lo == 60 && b.u.roll.rows == 12 &&
          b.u.roll.steps == 16 && b.u.roll.first == 16 && b.u.roll.total == 32 && b.u.roll.cur_col == 2 &&
          b.u.roll.cur_row == 4 && b.u.roll.play == 5 && b.u.roll.beat == 4 && !strcmp(b.u.roll.label, "C4-B4") &&
          b.u.roll.n == 3 && b.u.roll.note[2].col == 14 && b.u.roll.note[2].len == 2 && b.u.roll.note[2].row == 11 &&
          b.u.roll.note[2].vel == 64 && b.u.roll.note[2].flags == (R4P_RN_OUT | R4P_RN_MUTED) &&
          b.u.roll.note[1].flags == R4P_RN_SEL && b.u.roll.note[0].flags == R4P_RN_IN);
    /* B: the widest (24 rows up to 126, 32 steps), 14-bit positions, recording, no cursor, a Latin-1 label */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_ROLL; m.u.roll.flags = R4P_RF_PLAY | R4P_RF_REC; m.u.roll.lo = 103; m.u.roll.rows = 24; m.u.roll.steps = 32;
    m.u.roll.first = 288; m.u.roll.total = 300; m.u.roll.cur_col = R4P_ROLL_NONE; m.u.roll.cur_row = R4P_ROLL_NONE;
    m.u.roll.play = 31; m.u.roll.beat = 0; strcpy(m.u.roll.label, "T\xD3LF"); m.u.roll.n = 1;
    m.u.roll.note[0] = (r4p_rnote_t){31, 1, 23, 0, R4P_RN_CHANCE};
    n = enc(&m);
    CHECK(n == sizeof ROLL_B && !memcmp(buf, ROLL_B, n));
    CHECK(r4p_parse(ROLL_B, sizeof ROLL_B, &b) == 1 && b.u.roll.first == 288 && b.u.roll.total == 300 &&
          !strcmp(b.u.roll.label, "T\xD3LF") && b.u.roll.note[0].row == 23 && b.u.roll.note[0].flags == R4P_RN_CHANCE);
    /* the end of the view: rows 0, nothing else */
    memset(&m, 0, sizeof m);
    m.cmd = R4P_ROLL; m.u.roll.cur_col = m.u.roll.cur_row = m.u.roll.play = R4P_ROLL_NONE;
    n = enc(&m);
    CHECK(n == sizeof ROLL_END && !memcmp(buf, ROLL_END, n));
    CHECK(r4p_parse(ROLL_END, sizeof ROLL_END, &b) == 1 && b.u.roll.rows == 0 && b.u.roll.n == 0);
    /* the longest: 64 notes, a label of 24 Latin-1 characters, within the device's 640 */
    roll_a(&m);
    m.u.roll.rows = 24; m.u.roll.steps = 32; m.u.roll.n = 64;
    for (i = 0; i < 24; i++) m.u.roll.label[i] = (char)0xC4;
    m.u.roll.label[24] = 0;
    for (i = 0; i < 64; i++) m.u.roll.note[i] = (r4p_rnote_t){(uint8_t)(i % 32), 1, (uint8_t)(i % 24), 90, 0};
    n = enc(&m);
    CHECK(n == 5 + 12 + 1 + 48 + 1 + 64 * 5 + 1 && n <= R4P_FRAME_MAX && r4p_parse(buf, n, &b) == 1 && b.u.roll.n == 64 &&
          b.u.roll.note[63].col == 31 && b.u.roll.note[63].row == 15 && (uint8_t)b.u.roll.label[23] == 0xC4);
    /* refused: each rule once */
    roll_a(&m); m.u.roll.flags = 4; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.rows = 25; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.steps = 0; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.steps = 33; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.lo = 117; CHECK(enc(&m) == 0);                          /* 117 + 11 = 128 */
    roll_a(&m); m.u.roll.lo = 116; CHECK(round_trip(&m, &b) == 1);              /* up to 127 */
    roll_a(&m); m.u.roll.cur_col = 16; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.cur_row = 12; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.play = 16; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.beat = 33; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.n = 65; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.note[0].len = 0; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.note[2].len = 3; CHECK(enc(&m) == 0);                  /* 14 + 3 > 16 */
    roll_a(&m); m.u.roll.note[0].row = 12; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.note[0].flags = 32; CHECK(enc(&m) == 0);
    roll_a(&m); m.u.roll.note[0].vel = 128; CHECK(enc(&m) == 0);
    memset(&m, 0, sizeof m); m.cmd = R4P_ROLL; m.u.roll.cur_col = m.u.roll.cur_row = m.u.roll.play = R4P_ROLL_NONE;
    m.u.roll.steps = 4; CHECK(enc(&m) == 0);                                     /* an end with steps */
    m.u.roll.steps = 0; m.u.roll.play = 0; CHECK(enc(&m) == 0);                  /* an end with a playhead */
    {
        uint8_t trunc[sizeof ROLL_A - 1], tail[sizeof ROLL_A + 1], badn[sizeof ROLL_A], badc[sizeof ROLL_A];
        memcpy(trunc, ROLL_A, sizeof ROLL_A - 2); trunc[sizeof trunc - 1] = 0xF7;
        memcpy(tail, ROLL_A, sizeof ROLL_A - 1); tail[sizeof ROLL_A - 1] = 0x00; tail[sizeof ROLL_A] = 0xF7;
        memcpy(badn, ROLL_A, sizeof ROLL_A); badn[23] = 0x41;                      /* n 65 */
        memcpy(badc, ROLL_A, sizeof ROLL_A); badc[34] = 0x10;                      /* a note at column 16 */
        CHECK(r4p_parse(trunc, sizeof trunc, &b) == -1 && r4p_parse(tail, sizeof tail, &b) == -1 &&
              r4p_parse(badn, sizeof badn, &b) == -1 && r4p_parse(badc, sizeof badc, &b) == -1);
    }
    /* the 1.7 HELLO_ACK carries minor 7 */
    m.cmd = R4P_HELLO_ACK; m.u.ack.proto = 1; m.u.ack.minor = 7; CHECK(enc(&m) == 8 && round_trip(&m, &b) == 1 && b.u.ack.minor == 7);
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
    t_v13();
    t_v14();
    t_v15();
    t_v16();
    t_v17();
    printf("r4p: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
