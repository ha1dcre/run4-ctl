/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL protocol v1 codec (see r4p.h, docs/PROTOCOL.md). */
#include "r4p.h"

static const uint8_t HDR[4] = {0xF0, 0x7D, 0x52, 0x34};

/* ---------------------------------------------------------------- writer --- */
typedef struct { uint8_t *b; size_t cap, n; int bad; } wr_t;
static void w8(wr_t *w, unsigned v)
{
    if (v > 0x7Fu || w->n >= w->cap)
        w->bad = 1;
    else
        w->b[w->n++] = (uint8_t)v;
}
static void w14(wr_t *w, unsigned v)
{
    if (v > R4P_V14_MAX)
        w->bad = 1;
    w8(w, (v >> 7) & 0x7Fu);
    w8(w, v & 0x7Fu);
}
static void wstr(wr_t *w, const char *s)
{
    unsigned n = 0, i;
    while (n < R4P_STR_MAX && s[n])
        n++;
    w8(w, n);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x80u) {
            w8(w, 0x7F);
            w8(w, c - 0x80u);
        } else {
            w8(w, (c < 0x20u || c == 0x7Fu) ? '?' : c);
        }
    }
}

size_t r4p_encode(uint8_t *buf, size_t cap, const r4p_msg_t *m)
{
    wr_t w = {buf, cap, 0, 0};
    unsigned i;
    if (cap < 6)
        return 0;
    for (i = 0; i < 4; i++)
        w.b[w.n++] = HDR[i];
    w8(&w, m->cmd);
    switch (m->cmd) {
    case R4P_HELLO:
        w8(&w, m->u.hello.proto); w8(&w, m->u.hello.fw_major); w8(&w, m->u.hello.fw_minor); w8(&w, m->u.hello.mode);
        w8(&w, m->u.hello.minor);
        if (m->u.hello.mode > R4P_MODE_SYNTH)
            w.bad = 1;
        break;
    case R4P_BTN:
        w8(&w, m->u.btn.id); w8(&w, m->u.btn.state);
        if (m->u.btn.id >= R4P_NBTN || m->u.btn.state > 1)
            w.bad = 1;
        break;
    case R4P_ENC: {
        int d = m->u.enc.delta;
        if (m->u.enc.id >= R4P_NENC)
            w.bad = 1;
        d = d > 63 ? 63 : d < -63 ? -63 : d;
        w8(&w, m->u.enc.id); w8(&w, (unsigned)(0x40 + d));
        break;
    }
    case R4P_POT:
        w14(&w, m->u.pot.value);
        break;
    case R4P_SYNC_REQ:
        break;
    case R4P_HELLO_ACK:
        w8(&w, m->u.ack.proto);
        if (m->u.ack.minor)
            w8(&w, m->u.ack.minor);
        break;
    case R4P_LIST_PICK:
        w8(&w, m->u.pick.index); w8(&w, m->u.pick.action);
        if (m->u.pick.action > R4P_PICK_MOVE)
            w.bad = 1;
        break;
    case R4P_LIST: {
        const r4p_list_t *l = &m->u.list;
        wstr(&w, l->title);
        w8(&w, l->r); w8(&w, l->g); w8(&w, l->b);
        w8(&w, l->total); w8(&w, l->first); w8(&w, l->sel); w8(&w, l->n);
        if (l->n > R4P_LIST_ROWS || l->total > 127 || (unsigned)l->first + l->n > l->total || (l->total && l->sel >= l->total))
            w.bad = 1;
        for (i = 0; i < l->n && i < R4P_LIST_ROWS; i++)
            wstr(&w, l->item[i]);
        break;
    }
    case R4P_PAGE: {
        const r4p_page_t *p = &m->u.page;
        wstr(&w, p->title); wstr(&w, p->track);
        w8(&w, p->r); w8(&w, p->g); w8(&w, p->b);
        w8(&w, p->page_idx); w8(&w, p->page_count); w8(&w, p->bank_idx); w8(&w, p->bank_count);
        for (i = 0; i < R4P_NSLOTS; i++) {
            wstr(&w, p->slot[i].name);
            w8(&w, p->slot[i].kind);
            if (p->slot[i].kind >= R4P_KIND_COUNT)
                w.bad = 1;
            w14(&w, p->slot[i].value);
            wstr(&w, p->slot[i].text);
        }
        break;
    }
    case R4P_VALUE:
        w8(&w, m->u.value.slot);
        if (m->u.value.slot >= R4P_NSLOTS)
            w.bad = 1;
        w14(&w, m->u.value.value);
        wstr(&w, m->u.value.text);
        break;
    case R4P_LEDS: {
        uint8_t pk[R4P_LED_BYTES];
        r4p_leds_pack(&m->u.leds, pk);
        for (i = 0; i < R4P_LED_BYTES; i++)
            w8(&w, pk[i]);
        break;
    }
    case R4P_STATUS:
        w14(&w, m->u.status.bpm10); w14(&w, m->u.status.bar);
        w8(&w, m->u.status.beat); w8(&w, m->u.status.flags);
        if (m->u.status.beat > 15 || m->u.status.flags > 7)
            w.bad = 1;
        break;
    case R4P_TOAST:
        wstr(&w, m->u.toast.text);
        w8(&w, m->u.toast.dur100ms);
        break;
    default:
        return 0;
    }
    if (w.n >= w.cap)
        w.bad = 1;
    if (w.bad)
        return 0;
    w.b[w.n++] = 0xF7;
    return w.n;
}

/* ---------------------------------------------------------------- reader --- */
typedef struct { const uint8_t *p; size_t n, at; int bad; } rd_t;
static unsigned r8(rd_t *r)
{
    if (r->at >= r->n) {
        r->bad = 1;
        return 0;
    }
    return r->p[r->at++];
}
static unsigned r14(rd_t *r)
{
    unsigned hi = r8(r);
    return (hi << 7) | r8(r);
}
static void rstr(rd_t *r, char *dst)
{
    unsigned n = r8(r), i;
    if (n > R4P_STR_MAX) {
        r->bad = 1;
        n = 0;
    }
    for (i = 0; i < n && !r->bad; i++) {
        unsigned c = r8(r);
        if (c == 0x7Fu)
            c = 0x80u + r8(r);
        else if (c < 0x20u)
            r->bad = 1;
        dst[i] = (char)c;
    }
    dst[r->bad ? 0 : n] = 0;
}

int r4p_parse_body(const uint8_t *p, size_t n, r4p_msg_t *out)
{
    rd_t r = {p, n, 0, 0};
    unsigned i;
    if (n < 3 || p[0] != HDR[1] || p[1] != HDR[2] || p[2] != HDR[3])
        return 0;
    if (n < 4)
        return -1;
    for (i = 3; i < n; i++)             /* the body is 7-bit: a status byte inside means a damaged frame */
        if (p[i] > 0x7Fu)
            return -1;
    r.at = 3;
    out->cmd = (uint8_t)r8(&r);
    switch (out->cmd) {
    case R4P_HELLO:
        out->u.hello.proto = (uint8_t)r8(&r); out->u.hello.fw_major = (uint8_t)r8(&r);
        out->u.hello.fw_minor = (uint8_t)r8(&r); out->u.hello.mode = (uint8_t)r8(&r);
        out->u.hello.minor = r.at < r.n ? (uint8_t)r8(&r) : 0u;       /* optional since protocol 1.1 */
        if (out->u.hello.mode > R4P_MODE_SYNTH)
            r.bad = 1;
        break;
    case R4P_BTN:
        out->u.btn.id = (uint8_t)r8(&r); out->u.btn.state = (uint8_t)r8(&r);
        if (out->u.btn.id >= R4P_NBTN || out->u.btn.state > 1)
            r.bad = 1;
        break;
    case R4P_ENC: {
        unsigned d;
        out->u.enc.id = (uint8_t)r8(&r);
        d = r8(&r);
        out->u.enc.delta = (int8_t)((int)d - 0x40);
        if (out->u.enc.id >= R4P_NENC || d == 0)         /* 0x00 would be -64: the range is +-63 */
            r.bad = 1;
        break;
    }
    case R4P_POT:
        out->u.pot.value = (uint16_t)r14(&r);
        break;
    case R4P_SYNC_REQ:
        break;
    case R4P_HELLO_ACK:
        out->u.ack.proto = (uint8_t)r8(&r);
        out->u.ack.minor = r.at < r.n ? (uint8_t)r8(&r) : 0u;
        break;
    case R4P_LIST_PICK:
        out->u.pick.index = (uint8_t)r8(&r); out->u.pick.action = (uint8_t)r8(&r);
        if (out->u.pick.action > R4P_PICK_MOVE)
            r.bad = 1;
        break;
    case R4P_LIST: {
        r4p_list_t *l = &out->u.list;
        for (i = 0; i < sizeof *l; i++)
            ((uint8_t *)l)[i] = 0;
        rstr(&r, l->title);
        l->r = (uint8_t)r8(&r); l->g = (uint8_t)r8(&r); l->b = (uint8_t)r8(&r);
        l->total = (uint8_t)r8(&r); l->first = (uint8_t)r8(&r); l->sel = (uint8_t)r8(&r); l->n = (uint8_t)r8(&r);
        if (l->n > R4P_LIST_ROWS || (unsigned)l->first + l->n > l->total || (l->total && l->sel >= l->total))
            r.bad = 1;
        for (i = 0; i < l->n && !r.bad; i++)
            rstr(&r, l->item[i]);
        break;
    }
    case R4P_PAGE: {
        r4p_page_t *g = &out->u.page;
        rstr(&r, g->title); rstr(&r, g->track);
        g->r = (uint8_t)r8(&r); g->g = (uint8_t)r8(&r); g->b = (uint8_t)r8(&r);
        g->page_idx = (uint8_t)r8(&r); g->page_count = (uint8_t)r8(&r);
        g->bank_idx = (uint8_t)r8(&r); g->bank_count = (uint8_t)r8(&r);
        for (i = 0; i < R4P_NSLOTS; i++) {
            rstr(&r, g->slot[i].name);
            g->slot[i].kind = (uint8_t)r8(&r);
            if (g->slot[i].kind >= R4P_KIND_COUNT)
                r.bad = 1;
            g->slot[i].value = (uint16_t)r14(&r);
            rstr(&r, g->slot[i].text);
        }
        break;
    }
    case R4P_VALUE:
        out->u.value.slot = (uint8_t)r8(&r);
        if (out->u.value.slot >= R4P_NSLOTS)
            r.bad = 1;
        out->u.value.value = (uint16_t)r14(&r);
        rstr(&r, out->u.value.text);
        break;
    case R4P_LEDS: {
        uint8_t pk[R4P_LED_BYTES];
        for (i = 0; i < R4P_LED_BYTES; i++)
            pk[i] = (uint8_t)r8(&r);
        r4p_leds_unpack(pk, &out->u.leds);
        break;
    }
    case R4P_STATUS:
        out->u.status.bpm10 = (uint16_t)r14(&r); out->u.status.bar = (uint16_t)r14(&r);
        out->u.status.beat = (uint8_t)r8(&r); out->u.status.flags = (uint8_t)r8(&r);
        if (out->u.status.beat > 15 || out->u.status.flags > 7)
            r.bad = 1;
        break;
    case R4P_TOAST:
        rstr(&r, out->u.toast.text);
        out->u.toast.dur100ms = (uint8_t)r8(&r);
        break;
    default:
        return -1;
    }
    return (r.bad || r.at != r.n) ? -1 : 1;     /* truncated, or trailing bytes */
}

int r4p_parse(const uint8_t *f, size_t n, r4p_msg_t *out)
{
    if (n < 2 || f[0] != 0xF0 || f[n - 1] != 0xF7)
        return -1;
    return r4p_parse_body(f + 1, n - 2, out);
}

/* ----------------------------------------------------------- shorthands --- */
size_t r4p_hello(uint8_t *b, size_t c, uint8_t ma, uint8_t mi, uint8_t mode)
{
    r4p_msg_t m;
    m.cmd = R4P_HELLO; m.u.hello.proto = R4P_VER; m.u.hello.fw_major = ma; m.u.hello.fw_minor = mi; m.u.hello.mode = mode;
    m.u.hello.minor = R4P_MINOR;
    return r4p_encode(b, c, &m);
}
size_t r4p_btn(uint8_t *b, size_t c, uint8_t id, uint8_t down)
{
    r4p_msg_t m;
    m.cmd = R4P_BTN; m.u.btn.id = id; m.u.btn.state = down;
    return r4p_encode(b, c, &m);
}
size_t r4p_enc(uint8_t *b, size_t c, uint8_t id, int delta)
{
    r4p_msg_t m;
    m.cmd = R4P_ENC; m.u.enc.id = id; m.u.enc.delta = (int8_t)(delta > 63 ? 63 : delta < -63 ? -63 : delta);
    return r4p_encode(b, c, &m);
}
size_t r4p_pot(uint8_t *b, size_t c, uint16_t v)
{
    r4p_msg_t m;
    m.cmd = R4P_POT; m.u.pot.value = v;
    return r4p_encode(b, c, &m);
}
size_t r4p_sync_req(uint8_t *b, size_t c)
{
    r4p_msg_t m;
    m.cmd = R4P_SYNC_REQ;
    return r4p_encode(b, c, &m);
}

size_t r4p_list_pick(uint8_t *b, size_t c, uint8_t index, uint8_t action)
{
    r4p_msg_t m;
    m.cmd = R4P_LIST_PICK; m.u.pick.index = index; m.u.pick.action = action;
    return r4p_encode(b, c, &m);
}

void r4p_enc_add(r4p_enc_acc_t *a, uint8_t id, int delta)
{
    int v;
    if (id >= R4P_NENC)
        return;
    v = a->acc[id] + delta;
    a->acc[id] = (int16_t)(v > 32000 ? 32000 : v < -32000 ? -32000 : v);
}
int r4p_enc_take(r4p_enc_acc_t *a, uint8_t id)
{
    int v, t;
    if (id >= R4P_NENC)
        return 0;
    v = a->acc[id];
    t = v > 63 ? 63 : v < -63 ? -63 : v;
    a->acc[id] = (int16_t)(v - t);
    return t;
}

void r4p_leds_pack(const r4p_leds_t *l, uint8_t out[R4P_LED_BYTES])
{
    unsigned i;
    for (i = 0; i < R4P_LED_BYTES; i++)
        out[i] = 0;
    for (i = 0; i < R4P_NBTN + R4P_NKEY; i++) {
        unsigned s = (i < R4P_NBTN ? l->btn[i] : l->key[i - R4P_NBTN]) & 3u;
        out[i / 3] = (uint8_t)(out[i / 3] | (s << (2 * (i % 3))));
    }
}
void r4p_leds_unpack(const uint8_t in[R4P_LED_BYTES], r4p_leds_t *l)
{
    unsigned i;
    for (i = 0; i < R4P_NBTN + R4P_NKEY; i++) {
        uint8_t s = (uint8_t)((in[i / 3] >> (2 * (i % 3))) & 3u);
        if (i < R4P_NBTN)
            l->btn[i] = s;
        else
            l->key[i - R4P_NBTN] = s;
    }
}

/* ----------------------------------------------------------------- text --- */
size_t r4p_utf8_to_latin1(char *dst, size_t cap, const char *s)
{
    size_t n = 0;
    while (*s && n + 1 < cap) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80u) {
            dst[n++] = (char)c; s++;
        } else if ((c & 0xE0u) == 0xC0u && ((unsigned char)s[1] & 0xC0u) == 0x80u) {
            unsigned cp = ((c & 0x1Fu) << 6) | ((unsigned char)s[1] & 0x3Fu);
            dst[n++] = cp <= 0xFFu ? (char)cp : '?'; s += 2;
        } else {                                     /* longer sequences: one '?' for the whole character */
            dst[n++] = '?'; s++;
            while (((unsigned char)*s & 0xC0u) == 0x80u)
                s++;
        }
    }
    dst[n] = 0;
    return n;
}

char r4p_fold(unsigned char c)
{
    static const char T[] =                          /* U+00C0 .. U+00FF */
        "AAAAAAACEEEEIIII" "DNOOOOO*OUUUUYTs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
    if (c >= 0xC0u)
        return T[c - 0xC0u];
    if (c >= 0x80u || c < 0x20u)
        return '?';
    return (char)c;
}
void r4p_fold_str(char *dst, const char *src)
{
    while ((*dst++ = *src ? r4p_fold((unsigned char)*src) : 0), *src++)
        ;
}
