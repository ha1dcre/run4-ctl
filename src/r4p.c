/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL protocol v1 codec (see r4p.h, docs/PROTOCOL.md). */
#include "r4p.h"

static const uint8_t HDR[4] = {0xF0, 0x7D, 0x52, 0x34};
/* protocol 1.3: HV_PARAM's wire ranges (docs/PROTOCOL.md "Host voice"), 0..max each */
const uint8_t R4P_HVP_MAX[R4P_HVP_COUNT] = {31, 7, 127, 127, 100, 99, 99, 99, 63, 12, 100, 48, 100, 100, 100, 100, 100, 5};
static int voice_idx_ok(unsigned v) { return v < R4P_NVOICES || v == R4P_VOICE_HV; }

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
static void wstr_n(wr_t *w, const char *s, unsigned max)
{
    unsigned n = 0, i;
    while (n < max && s[n])
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
static void wstr(wr_t *w, const char *s) { wstr_n(w, s, R4P_STR_MAX); }
static void wbytes(wr_t *w, const uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        w8(w, p[i]);
}
static void wcrc(wr_t *w, uint32_t c)               /* 32 bits as five 7-bit groups, the lowest first */
{
    unsigned i;
    for (i = 0; i < 5u; i++)
        w8(w, (c >> (7u * i)) & 0x7Fu);
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
        if (p->v12) {                        /* protocol 1.2 trailer */
            w8(&w, p->cursor); w14(&w, p->item_idx); w14(&w, p->item_count); wstr(&w, p->path);
            if ((p->cursor >= R4P_NSLOTS && p->cursor != R4P_CURSOR_NONE) ||
                (p->item_count ? p->item_idx >= p->item_count : p->item_idx != 0))
                w.bad = 1;
        }
        break;
    }
    case R4P_FOCUS: {
        const r4p_focus_t *f = &m->u.focus;
        w8(&w, f->cursor); w14(&w, f->item_idx); w14(&w, f->item_count);
        if ((f->cursor >= R4P_NSLOTS && f->cursor != R4P_CURSOR_NONE) || (f->item_count ? f->item_idx >= f->item_count : f->item_idx != 0))
            w.bad = 1;
        break;
    }
    case R4P_VALUE:
        w8(&w, m->u.value.slot);
        if (m->u.value.slot >= R4P_NSLOTS)
            w.bad = 1;
        w14(&w, m->u.value.value);
        wstr(&w, m->u.value.text);
        if (m->u.value.flags) {              /* protocol 1.2 */
            w8(&w, m->u.value.flags);
            if (m->u.value.flags & ~R4P_VF_BIG)
                w.bad = 1;
        }
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
    /* ------------------------------------------------------------- 1.3 --- */
    case R4P_HV_STATE: {
        const r4p_hvstate_t *s = &m->u.hvstate;
        w8(&w, s->state); w8(&w, s->chan); w8(&w, s->voice); wstr(&w, s->name);
        if (s->state > R4P_HV_SUSPENDED || s->chan > 15 || !voice_idx_ok(s->voice))
            w.bad = 1;
        break;
    }
    case R4P_VOICE_NAMES: {
        const r4p_names_t *l = &m->u.names;
        w8(&w, l->first); w8(&w, l->total); w8(&w, l->n);
        if (!l->n || l->n > R4P_NAMES_ROWS || l->total > 127 || (unsigned)l->first + l->n > l->total)
            w.bad = 1;
        for (i = 0; i < l->n && i < R4P_NAMES_ROWS; i++) {
            unsigned k = 0;
            while (k <= R4P_NAME_MAX && l->name[i][k])
                k++;
            w8(&w, l->used[i]);
            if (l->used[i] > 1 || k > R4P_NAME_MAX)
                w.bad = 1;
            wstr_n(&w, l->name[i], R4P_NAME_MAX);
        }
        break;
    }
    case R4P_VOICE_DATA:
        w8(&w, m->u.voice.idx); w8(&w, m->u.voice.used);
        if (!voice_idx_ok(m->u.voice.idx) || m->u.voice.used > 1)
            w.bad = 1;
        if (m->u.voice.used)
            wbytes(&w, m->u.voice.data, R4P_PACKED);
        break;
    case R4P_BK_PART: case R4P_BK_DATA:
        w8(&w, m->u.voice.idx); w8(&w, m->u.voice.used);
        if (m->u.voice.idx >= R4P_BK_VOICES || m->u.voice.used > 1)
            w.bad = 1;
        wbytes(&w, m->u.voice.data, R4P_PACKED);
        break;
    case R4P_BK_END: case R4P_BK_INFO:
        w8(&w, m->u.bksum.count); wcrc(&w, m->u.bksum.crc);
        if (m->u.bksum.count > R4P_BK_VOICES)
            w.bad = 1;
        break;
    case R4P_BK_GET:
        w8(&w, m->u.b1.v);
        if (m->u.b1.v >= R4P_BK_VOICES && m->u.b1.v != R4P_BK_SUMMARY)
            w.bad = 1;
        break;
    case R4P_HV_ON:
        w8(&w, m->u.hvon.on); w8(&w, m->u.hvon.chan);
        if (m->u.hvon.on > 1 || m->u.hvon.chan > 15)
            w.bad = 1;
        break;
    case R4P_HV_VOICE: {
        const r4p_hvvoice_t *v = &m->u.hvvoice;
        w8(&w, v->kind);
        if (v->kind == R4P_HVV_STORED) {
            w8(&w, v->idx);
            if (v->idx >= R4P_NVOICES)
                w.bad = 1;
        } else if (v->kind == R4P_HVV_PACKED) {
            wbytes(&w, v->data, R4P_PACKED);
        } else if (v->kind == R4P_HVV_VCED) {
            wbytes(&w, v->data, R4P_VCED);
        } else {
            w.bad = 1;
        }
        break;
    }
    case R4P_HV_PARAM: {
        const r4p_hvparam_t *q = &m->u.hvparam;
        if (!q->n || q->n > R4P_HVP_COUNT)
            w.bad = 1;
        for (i = 0; i < q->n && i < R4P_HVP_COUNT; i++) {
            w8(&w, q->id[i]); w8(&w, q->val[i]);
            if (q->id[i] >= R4P_HVP_COUNT || q->val[i] > R4P_HVP_MAX[q->id[i]])
                w.bad = 1;
        }
        break;
    }
    case R4P_NAMES_REQ:
        break;
    case R4P_VOICE_GET:
        w8(&w, m->u.b1.v);
        if (!voice_idx_ok(m->u.b1.v))
            w.bad = 1;
        break;
    case R4P_TAPE_WAVE: {
        const r4p_wave_t *t = &m->u.wave;
        w8(&w, t->track); wstr(&w, t->label); w8(&w, t->n);
        if (t->track > 3 || t->n > R4P_WAVE_MAX)
            w.bad = 1;
        for (i = 0; i < t->n && i < R4P_WAVE_MAX; i++)
            w8(&w, t->peak[i]);
        break;
    }
    case R4P_TAPE_POS: {
        const r4p_tpos_t *t = &m->u.tpos;
        w14(&w, t->play); w14(&w, t->in); w14(&w, t->out); w8(&w, t->flags);
        if (t->flags > 7 || (!(t->flags & R4P_TP_NOLOOP) && t->in > t->out))
            w.bad = 1;
        break;
    }
    case R4P_METERS:
        wbytes(&w, m->u.meters.level, R4P_NMETERS); w8(&w, m->u.meters.clip);
        if (m->u.meters.clip > 31)
            w.bad = 1;
        break;
    case R4P_BK_ACK:
        w8(&w, m->u.b1.v);
        if (m->u.b1.v > R4P_BK_FAILED)
            w.bad = 1;
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
static void rstr_n(rd_t *r, char *dst, unsigned max)
{
    unsigned n = r8(r), i;
    if (n > max) {
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
static void rstr(rd_t *r, char *dst) { rstr_n(r, dst, R4P_STR_MAX); }
static void rbytes(rd_t *r, uint8_t *dst, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        dst[i] = (uint8_t)r8(r);
}
static uint32_t rcrc(rd_t *r)
{
    uint32_t c = 0;
    unsigned i, b;
    for (i = 0; i < 5u; i++) {
        b = r8(r);
        if (i == 4u && b > 0x0Fu)                    /* the top group holds bits 28..31 only */
            r->bad = 1;
        c |= (uint32_t)b << (7u * i);
    }
    return c;
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
        g->v12 = 0;
        g->cursor = R4P_CURSOR_NONE;
        g->item_idx = g->item_count = 0;
        g->path[0] = 0;
        if (!r.bad && r.at < r.n) {          /* protocol 1.2 trailer: all of it */
            g->v12 = 1;
            g->cursor = (uint8_t)r8(&r);
            g->item_idx = (uint16_t)r14(&r);
            g->item_count = (uint16_t)r14(&r);
            rstr(&r, g->path);
            if ((g->cursor >= R4P_NSLOTS && g->cursor != R4P_CURSOR_NONE) ||
                (g->item_count ? g->item_idx >= g->item_count : g->item_idx != 0))
                r.bad = 1;
        }
        break;
    }
    case R4P_FOCUS: {
        r4p_focus_t *f = &out->u.focus;
        f->cursor = (uint8_t)r8(&r);
        f->item_idx = (uint16_t)r14(&r);
        f->item_count = (uint16_t)r14(&r);
        if ((f->cursor >= R4P_NSLOTS && f->cursor != R4P_CURSOR_NONE) || (f->item_count ? f->item_idx >= f->item_count : f->item_idx != 0))
            r.bad = 1;
        break;
    }
    case R4P_VALUE:
        out->u.value.slot = (uint8_t)r8(&r);
        if (out->u.value.slot >= R4P_NSLOTS)
            r.bad = 1;
        out->u.value.value = (uint16_t)r14(&r);
        rstr(&r, out->u.value.text);
        out->u.value.flags = !r.bad && r.at < r.n ? (uint8_t)r8(&r) : 0u;     /* protocol 1.2 */
        if (out->u.value.flags & ~R4P_VF_BIG)
            r.bad = 1;
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
    /* ------------------------------------------------------------- 1.3 --- */
    case R4P_HV_STATE: {
        r4p_hvstate_t *s = &out->u.hvstate;
        s->state = (uint8_t)r8(&r); s->chan = (uint8_t)r8(&r); s->voice = (uint8_t)r8(&r);
        rstr(&r, s->name);
        if (s->state > R4P_HV_SUSPENDED || s->chan > 15 || !voice_idx_ok(s->voice))
            r.bad = 1;
        break;
    }
    case R4P_VOICE_NAMES: {
        r4p_names_t *l = &out->u.names;
        for (i = 0; i < sizeof *l; i++)
            ((uint8_t *)l)[i] = 0;
        l->first = (uint8_t)r8(&r); l->total = (uint8_t)r8(&r); l->n = (uint8_t)r8(&r);
        if (!l->n || l->n > R4P_NAMES_ROWS || (unsigned)l->first + l->n > l->total)
            r.bad = 1;
        for (i = 0; i < l->n && !r.bad; i++) {
            l->used[i] = (uint8_t)r8(&r);
            if (l->used[i] > 1)
                r.bad = 1;
            rstr_n(&r, l->name[i], R4P_NAME_MAX);
        }
        break;
    }
    case R4P_VOICE_DATA:
        out->u.voice.idx = (uint8_t)r8(&r); out->u.voice.used = (uint8_t)r8(&r);
        if (!voice_idx_ok(out->u.voice.idx) || out->u.voice.used > 1)
            r.bad = 1;
        for (i = 0; i < R4P_PACKED; i++)
            out->u.voice.data[i] = 0;
        if (out->u.voice.used && !r.bad)
            rbytes(&r, out->u.voice.data, R4P_PACKED);
        break;
    case R4P_BK_PART: case R4P_BK_DATA:
        out->u.voice.idx = (uint8_t)r8(&r); out->u.voice.used = (uint8_t)r8(&r);
        if (out->u.voice.idx >= R4P_BK_VOICES || out->u.voice.used > 1)
            r.bad = 1;
        rbytes(&r, out->u.voice.data, R4P_PACKED);
        break;
    case R4P_BK_END: case R4P_BK_INFO:
        out->u.bksum.count = (uint8_t)r8(&r);
        out->u.bksum.crc = rcrc(&r);
        if (out->u.bksum.count > R4P_BK_VOICES)
            r.bad = 1;
        break;
    case R4P_BK_GET:
        out->u.b1.v = (uint8_t)r8(&r);
        if (out->u.b1.v >= R4P_BK_VOICES && out->u.b1.v != R4P_BK_SUMMARY)
            r.bad = 1;
        break;
    case R4P_HV_ON:
        out->u.hvon.on = (uint8_t)r8(&r); out->u.hvon.chan = (uint8_t)r8(&r);
        if (out->u.hvon.on > 1 || out->u.hvon.chan > 15)
            r.bad = 1;
        break;
    case R4P_HV_VOICE: {
        r4p_hvvoice_t *v = &out->u.hvvoice;
        v->kind = (uint8_t)r8(&r);
        v->idx = 0;
        if (v->kind == R4P_HVV_STORED) {
            v->idx = (uint8_t)r8(&r);
            if (v->idx >= R4P_NVOICES)
                r.bad = 1;
        } else if (v->kind == R4P_HVV_PACKED) {
            rbytes(&r, v->data, R4P_PACKED);
        } else if (v->kind == R4P_HVV_VCED) {
            rbytes(&r, v->data, R4P_VCED);
        } else {
            r.bad = 1;
        }
        break;
    }
    case R4P_HV_PARAM: {
        r4p_hvparam_t *q = &out->u.hvparam;
        q->n = 0;
        while (r.at < r.n && !r.bad) {
            if (q->n >= R4P_HVP_COUNT) {
                r.bad = 1;
                break;
            }
            q->id[q->n] = (uint8_t)r8(&r);
            q->val[q->n] = (uint8_t)r8(&r);
            if (q->id[q->n] >= R4P_HVP_COUNT || q->val[q->n] > R4P_HVP_MAX[q->id[q->n]])
                r.bad = 1;
            q->n++;
        }
        if (!q->n)
            r.bad = 1;
        break;
    }
    case R4P_NAMES_REQ:
        break;
    case R4P_VOICE_GET:
        out->u.b1.v = (uint8_t)r8(&r);
        if (!voice_idx_ok(out->u.b1.v))
            r.bad = 1;
        break;
    case R4P_TAPE_WAVE: {
        r4p_wave_t *t = &out->u.wave;
        t->track = (uint8_t)r8(&r);
        rstr(&r, t->label);
        t->n = (uint8_t)r8(&r);
        if (t->track > 3 || t->n > R4P_WAVE_MAX)
            r.bad = 1;
        for (i = 0; i < t->n && !r.bad; i++)
            t->peak[i] = (uint8_t)r8(&r);
        break;
    }
    case R4P_TAPE_POS: {
        r4p_tpos_t *t = &out->u.tpos;
        t->play = (uint16_t)r14(&r); t->in = (uint16_t)r14(&r); t->out = (uint16_t)r14(&r);
        t->flags = (uint8_t)r8(&r);
        if (t->flags > 7 || (!(t->flags & R4P_TP_NOLOOP) && t->in > t->out))
            r.bad = 1;
        break;
    }
    case R4P_METERS:
        rbytes(&r, out->u.meters.level, R4P_NMETERS);
        out->u.meters.clip = (uint8_t)r8(&r);
        if (out->u.meters.clip > 31)
            r.bad = 1;
        break;
    case R4P_BK_ACK:
        out->u.b1.v = (uint8_t)r8(&r);
        if (out->u.b1.v > R4P_BK_FAILED)
            r.bad = 1;
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

uint32_t r4p_crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    size_t i;
    unsigned k;
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8u; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
uint32_t r4p_crc32_part(uint32_t crc, const r4p_voice_t *part)
{
    uint8_t h[2];
    h[0] = part->idx;
    h[1] = part->used;
    return r4p_crc32(r4p_crc32(crc, h, 2), part->data, R4P_PACKED);
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
