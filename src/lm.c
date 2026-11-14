/* SPDX-License-Identifier: GPL-3.0-only
 * RUN4 CTL, protocol 1.4: the local modes for the host (docs/PROTOCOL.md "Protocol 1.4"). The host lists the modes,
 * switches them, and gets the playing local mode's self-description: its pages and parameters (lm_desc_t, one per
 * mode: src/synth_lm.c, src/amb_lm.c), then their values as they change and a status line. Everything here is
 * table-driven: a new mode only adds its lm_desc_t. Part of the unity build (app.c), main-loop context only. */

typedef struct {
    uint16_t id;                             /* stable: the host keeps it */
    uint8_t page, kind, flags, step;         /* R4P_LK_*, R4P_PF_* */
    int16_t lo, hi, def;
    uint8_t nlabels;
    const char *name, *unit;
} lm_param_t;
typedef struct {
    uint8_t id;                              /* R4P_MID_* (= the firmware's MODE_*) */
    const char *title;
    uint8_t npages;
    const char *const *pages;
    uint16_t nparams;
    uint8_t nstate;
    const char *const *state;                /* the LSTATE texts' labels */
    void (*param)(uint32_t i, lm_param_t *p);            /* parameter i's static description */
    void (*label)(uint32_t i, uint32_t k, char *t);      /* its label k (ENUM, MASK, TOGGLE), <= 16 characters */
    int (*get)(uint32_t i);                              /* its value now */
    void (*text)(uint32_t i, int v, char *t);            /* the value as the device shows it, <= 16 characters */
    int (*set)(uint32_t i, int v);                       /* R4P_LE_OK or why not (the range is checked before) */
    void (*lstate)(r4p_lstate_t *s);                     /* the status line */
} lm_desc_t;

#define LM_MAX 160                           /* parameters a description may have here (Synth mode's 147) */
#define LM_PERIOD_MS 67u                     /* value changes and the status line: at most 15 a second */
#define LM_NERR 4
#define LM_NMODES 3
static const char *const LM_MNAME[LM_NMODES] = {"RUN4", "SYNTH", "AMB"};
static const lm_desc_t *lm_desc(uint32_t mode);         /* (below: the modes' tables) */

enum { LMS_IDLE, LMS_HEAD, LMS_PAGES, LMS_PARAMS, LMS_END };
static struct {
    uint8_t sub;                             /* the host asked for this mode's description or values: it gets changes */
    uint8_t want_mode, want_modes, want_state;
    uint8_t pending;                         /* MODE_SET: the mode + 1, switched in input() (not inside app_sysex) */
    uint8_t st, label;                       /* the description under way: where; the next label of the param */
    uint16_t si;
    uint16_t scan;                           /* the value pass: the next parameter to look at */
    uint8_t err_n;
    r4p_lerr_t err[LM_NERR];
    uint32_t pass_at, state_at, state_sig;
    uint32_t hash[LM_NMODES];                /* each description's hash, once worked out (0: not yet) */
    uint32_t sets, frames;                   /* host-simulator checks: PSET pairs applied, frames sent */
    int16_t shadow[LM_MAX];                  /* the value last sent */
    uint32_t known[(LM_MAX + 31) / 32];      /* ... is valid */
} lm;

static int host14(void) { return ctl.session && ui.connected && ctl.host_minor >= 4; }
static int lm_avail(uint32_t m) { return m == MODE_RUN4 || m == MODE_SYNTH || (m == MODE_AMB && R4_AMB); }
static uint8_t lm_mflags(uint32_t m)
{
    return (uint8_t)((lm_avail(m) ? R4P_MF_AVAIL : 0) | (lm_desc(m) ? R4P_MF_DESC : 0) | (m == MODE_RUN4 ? R4P_MF_CTRL : 0));
}
static void lm_forget_all(void) { memset(lm.known, 0, sizeof lm.known); }
static void lm_forget(uint32_t i) { if (i < LM_MAX) lm.known[i / 32] &= ~(1u << (i % 32)); }
static void lm_err(uint8_t code, uint16_t id)
{
    if (lm.err_n >= LM_NERR)
        return;                              /* (a burst: the first ones say enough) */
    lm.err[lm.err_n].mode = mode;
    lm.err[lm.err_n].code = code;
    lm.err[lm.err_n].id = id;
    lm.err_n++;
}
/* a new mode, a new link, a new HELLO_ACK: nothing is owed, nothing subscribed; a 1.4 host is told the mode */
static void lm_reset(int tell)
{
    lm.sub = 0;
    lm.st = LMS_IDLE;
    lm.scan = 0;
    lm.err_n = 0;
    lm.want_state = 0;
    lm.want_modes = 0;
    lm_forget_all();
    lm.want_mode = (uint8_t)(tell != 0);
}
static void lm_link_lost(void) { lm_reset(0); lm.pending = 0; }

/* ------------------------------------------------------------ the frames --- */
static void lm_str(char *dst, const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        dst[i] = s[i];
    dst[i] = 0;
}
static void lm_head(const lm_desc_t *d, r4p_msg_t *m, uint32_t hash)
{
    uint32_t i;
    memset(&m->u.dhead, 0, sizeof m->u.dhead);
    m->cmd = R4P_DESC_HEAD;
    m->u.dhead.mode = d->id;
    m->u.dhead.hash = hash;
    m->u.dhead.npages = d->npages;
    m->u.dhead.nparams = d->nparams;
    m->u.dhead.nstate = d->nstate;
    lm_str(m->u.dhead.title, d->title, R4P_LM_STR);
    for (i = 0; i < d->nstate && i < R4P_LM_NSTATE; i++)
        lm_str(m->u.dhead.state[i], d->state[i], R4P_LM_STR);
}
static void lm_page(const lm_desc_t *d, uint32_t i, r4p_msg_t *m)
{
    m->cmd = R4P_DESC_PAGE;
    m->u.dpage.page = (uint8_t)i;
    lm_str(m->u.dpage.title, d->pages[i], R4P_LM_STR);
}
static int lm_value(const lm_desc_t *d, uint32_t i, const lm_param_t *p)
{
    int v = d->get(i);
    return v < p->lo ? p->lo : v > p->hi ? p->hi : v;
}
static void lm_text(const lm_desc_t *d, uint32_t i, int v, char *t)
{
    char b[32];
    b[0] = 0;
    d->text(i, v, b);
    lm_str(t, b, R4P_LM_STR);
}
static void lm_param(const lm_desc_t *d, uint32_t i, r4p_msg_t *m, int *v)
{
    lm_param_t p;
    r4p_dparam_t *q = &m->u.dparam;
    d->param(i, &p);
    memset(q, 0, sizeof *q);
    m->cmd = R4P_DESC_PARAM;
    q->id = p.id; q->page = p.page; q->kind = p.kind; q->flags = p.flags; q->step = p.step;
    q->lo = p.lo; q->hi = p.hi; q->def = p.def; q->nlabels = p.nlabels;
    lm_str(q->name, p.name, R4P_LM_STR);
    lm_str(q->unit, p.unit, R4P_LM_UNIT);
    *v = lm_value(d, i, &p);
    q->value = (int16_t)*v;
    lm_text(d, i, *v, q->text);
}
/* the labels of parameter i from `first`, as many as fit a frame; returns how many (0: none left) */
static uint32_t lm_labels(const lm_desc_t *d, uint32_t i, uint32_t first, r4p_msg_t *m, uint8_t *f, size_t cap, size_t *len)
{
    lm_param_t p;
    uint32_t n, k;
    d->param(i, &p);
    if (first >= p.nlabels)
        return 0;
    n = p.nlabels - first;
    n = n > R4P_LM_LROWS ? R4P_LM_LROWS : n;
    m->cmd = R4P_DESC_LABELS;
    memset(&m->u.dlabels, 0, sizeof m->u.dlabels);
    m->u.dlabels.id = p.id;
    m->u.dlabels.first = (uint8_t)first;
    m->u.dlabels.total = p.nlabels;
    for (k = 0; k < n; k++) {
        char b[32];
        b[0] = 0;
        d->label(i, first + k, b);
        lm_str(m->u.dlabels.label[k], b, R4P_LM_STR);
    }
    for (; n; n--) {                         /* (long or escaped labels: fewer in this frame) */
        m->u.dlabels.n = (uint8_t)n;
        if ((*len = r4p_encode(f, cap, m)) != 0)
            return n;
    }
    return 0;
}
static uint8_t lm_f[200];
static r4p_msg_t lm_m;                       /* (every frame is built here, off the main loop's stack; the hash first) */
/* the description's hash: every frame of it encoded once, as it goes out (the values left out) */
static uint32_t lm_hash_of(const lm_desc_t *d)
{
    uint32_t h = R4P_FNV0, i, first, n;
    size_t len;
    int v;
    lm_head(d, &lm_m, 0);
    h = r4p_desc_hash_add(h, lm_f, r4p_encode(lm_f, sizeof lm_f, &lm_m));
    for (i = 0; i < d->npages; i++) {
        lm_page(d, i, &lm_m);
        h = r4p_desc_hash_add(h, lm_f, r4p_encode(lm_f, sizeof lm_f, &lm_m));
    }
    for (i = 0; i < d->nparams; i++) {
        lm_param(d, i, &lm_m, &v);
        h = r4p_desc_hash_add(h, lm_f, r4p_encode(lm_f, sizeof lm_f, &lm_m));
        for (first = 0; (n = lm_labels(d, i, first, &lm_m, lm_f, sizeof lm_f, &len)) != 0; first += n)
            h = r4p_desc_hash_add(h, lm_f, len);
    }
    return h ? h : 1u;                       /* (0 means "no description" on the wire) */
}
static uint32_t lm_hash(uint32_t md)
{
    const lm_desc_t *d = lm_desc(md);
    if (!d || md >= LM_NMODES)
        return 0;
    if (!lm.hash[md])
        lm.hash[md] = lm_hash_of(d);
    return lm.hash[md];
}
static int lm_find(const lm_desc_t *d, uint32_t id, lm_param_t *p)       /* the index of the parameter with that id */
{
    uint32_t i;
    for (i = 0; i < d->nparams; i++) {
        d->param(i, p);
        if (p->id == id)
            return (int)i;
    }
    return -1;
}

/* ---------------------------------------------------------- host frames --- */
static void lm_pset(const r4p_pset_t *q)
{
    const lm_desc_t *d = lm_desc(mode);
    lm_param_t p;
    int idx[R4P_PSET_MAX];
    uint32_t k;
    if (q->mode != mode) { lm_err(R4P_LE_WRONGMODE, 0); lm.want_mode = 1; return; }
    if (!d) { lm_err(R4P_LE_NODESC, 0); return; }
    for (k = 0; k < q->n; k++) {             /* the whole frame first: one bad pair and none is applied */
        int v = q->v[k];
        idx[k] = lm_find(d, q->id[k], &p);
        if (idx[k] < 0) { lm_err(R4P_LE_UNKNOWN, q->id[k]); return; }
        if (p.flags & R4P_PF_READONLY) { lm_err(R4P_LE_READONLY, q->id[k]); return; }
        if (v < p.lo || v > p.hi || (v - p.lo) % p.step || (p.kind == R4P_LK_ACTION && v != 1)) {
            lm_err(R4P_LE_RANGE, q->id[k]);
            return;
        }
    }
    for (k = 0; k < q->n; k++) {             /* then each in turn; one the mode refuses now is skipped */
        int rc = d->set((uint32_t)idx[k], q->v[k]);
        if (rc != R4P_LE_OK)
            lm_err((uint8_t)rc, q->id[k]);
        else
            lm.sets++;
    }
}
/* 1.4 frames from the host (after its HELLO_ACK, any minor; answers go to a 1.4 host only) */
static void lm_frame(const r4p_msg_t *m)
{
    uint32_t v = m->u.b1.v;
    switch (m->cmd) {
    case R4P_MODES_REQ:
        lm.want_modes = 1;
        break;
    case R4P_MODE_SET:
        if (!lm_avail(v)) lm_err(R4P_LE_NOMODE, 0);
        else if (v == mode) lm.want_mode = 1;
        else lm.pending = (uint8_t)(v + 1u);
        break;
    case R4P_DESC_REQ: case R4P_VALS_REQ:
        if (v != mode) { lm_err(R4P_LE_WRONGMODE, 0); lm.want_mode = 1; break; }
        if (!lm_desc(mode)) { lm_err(R4P_LE_NODESC, 0); break; }
        lm.sub = 1;
        lm.want_state = 1;
        if (m->cmd == R4P_DESC_REQ) {
            lm.st = LMS_HEAD;                /* (a request while one goes out starts it over) */
            lm.si = 0;
            lm.label = 0;
        } else {
            lm_forget_all();                 /* every value again */
            lm.scan = 0;
            lm.pass_at = plat_ms() - LM_PERIOD_MS;
        }
        break;
    case R4P_PSET:
        lm_pset(&m->u.pset);
        break;
    default:
        break;
    }
}
static void lm_switch_owed(void)            /* input(): a MODE_SET, carried out as SEL + HOME would */
{
    if (lm.pending) {
        uint8_t to = (uint8_t)(lm.pending - 1u);
        lm.pending = 0;
        mode_switch(to);
        lm.want_mode = 1;
    }
}

/* ---------------------------------------------------------- what goes out --- */
static int lm_send(const r4p_msg_t *m)
{
    size_t n = r4p_encode(lm_f, sizeof lm_f, m);
    if (!n || !send_frame(lm_f, n))
        return 0;
    lm.frames++;
    return 1;
}
static int lm_stream(const lm_desc_t *d)    /* the next frame of the description; 1 = sent */
{
    size_t len;
    int v;
    switch (lm.st) {
    case LMS_HEAD: {
        uint32_t h = lm_hash(mode);
        lm_head(d, &lm_m, h);
        if (!lm_send(&lm_m))
            return 0;
        lm.st = d->npages ? LMS_PAGES : d->nparams ? LMS_PARAMS : LMS_END;
        lm.si = 0;
        return 1;
    }
    case LMS_PAGES:
        lm_page(d, lm.si, &lm_m);
        if (!lm_send(&lm_m))
            return 0;
        if (++lm.si >= d->npages) {
            lm.st = d->nparams ? LMS_PARAMS : LMS_END;
            lm.si = 0;
            lm.label = 0;
        }
        return 1;
    case LMS_PARAMS:
        if (lm.label == 0) {                 /* the parameter, its value as now: the value pass knows it was sent */
            lm_param(d, lm.si, &lm_m, &v);
            if (!lm_send(&lm_m))
                return 0;
            if (lm.si < LM_MAX) {
                lm.shadow[lm.si] = (int16_t)v;
                lm.known[lm.si / 32] |= 1u << (lm.si % 32);
            }
            lm.label = 1;
        } else {                             /* then its labels (label - 1 = the first not sent) */
            uint32_t n = lm_labels(d, lm.si, lm.label - 1u, &lm_m, lm_f, sizeof lm_f, &len);
            if (n) {
                if (!send_frame(lm_f, len))
                    return 0;
                lm.frames++;
                lm.label = (uint8_t)(lm.label + n);
                return 1;
            }
            lm.label = 0;
            if (++lm.si >= d->nparams)
                lm.st = LMS_END;
            return lm_stream(d);
        }
        return 1;
    case LMS_END: {
        uint32_t h = lm_hash(mode);
        lm_m.cmd = R4P_DESC_END;
        lm_m.u.mode.mode = d->id;
        lm_m.u.mode.flags = 0;
        lm_m.u.mode.hash = h;
        if (!lm_send(&lm_m))
            return 0;
        lm.st = LMS_IDLE;
        lm.want_state = 1;
        return 1;
    }
    default:
        return 0;
    }
}
static int lm_values(const lm_desc_t *d, uint32_t now)   /* the changed values, a frame of them; 1 = sent */
{
    lm_param_t p;
    uint32_t i, n = d->nparams < LM_MAX ? d->nparams : LM_MAX, last = n;
    uint16_t ix[R4P_PVAL_MAX];
    size_t len = 0;
    if (lm.scan == 0 && now - lm.pass_at < LM_PERIOD_MS)
        return 0;
    if (lm.scan == 0)
        lm.pass_at = now;
    memset(&lm_m.u.pval, 0, sizeof lm_m.u.pval);
    lm_m.cmd = R4P_PVAL;
    lm_m.u.pval.mode = d->id;
    for (i = lm.scan; i < n; i++) {
        int v;
        uint32_t k = lm_m.u.pval.n;
        d->param(i, &p);
        if (p.kind == R4P_LK_ACTION) {       /* (an action has no value to tell) */
            lm.known[i / 32] |= 1u << (i % 32);
            continue;
        }
        v = lm_value(d, i, &p);
        if (((lm.known[i / 32] >> (i % 32)) & 1u) && lm.shadow[i] == v)
            continue;
        ix[k] = (uint16_t)i;
        lm_m.u.pval.id[k] = p.id;
        lm_m.u.pval.v[k] = (int16_t)v;
        lm_text(d, i, v, lm_m.u.pval.text[k]);
        lm_m.u.pval.n = (uint8_t)(k + 1u);
        if (!(len = r4p_encode(lm_f, sizeof lm_f, &lm_m))) {   /* full: this one goes in the next frame */
            lm_m.u.pval.n = (uint8_t)k;
            last = i;
            break;
        }
        if (lm_m.u.pval.n >= R4P_PVAL_MAX) {
            last = i + 1u;
            break;
        }
    }
    if (!lm_m.u.pval.n) {
        lm.scan = 0;                         /* (the pass is over) */
        return 0;
    }
    if (!(len = r4p_encode(lm_f, sizeof lm_f, &lm_m)) || !send_frame(lm_f, len))
        return 0;                            /* (no room: the same values again next time) */
    lm.frames++;
    for (i = 0; i < lm_m.u.pval.n; i++) {       /* what went is known now, as it went (the engine may move on) */
        lm.shadow[ix[i]] = lm_m.u.pval.v[i];
        lm.known[ix[i] / 32] |= 1u << (ix[i] % 32);
    }
    lm.scan = (uint16_t)(last >= n ? 0u : last);
    return 1;
}
static int lm_state(const lm_desc_t *d, uint32_t now)
{
    size_t len;
    uint32_t sig;
    if (now - lm.state_at < LM_PERIOD_MS)
        return 0;
    memset(&lm_m.u.lstate, 0, sizeof lm_m.u.lstate);
    lm_m.cmd = R4P_LSTATE;
    d->lstate(&lm_m.u.lstate);
    lm_m.u.lstate.mode = d->id;
    if (!(len = r4p_encode(lm_f, sizeof lm_f, &lm_m)))
        return 0;
    sig = r4p_fnv(R4P_FNV0, lm_f, len);
    if (sig == lm.state_sig && !lm.want_state)
        return 0;
    if (!send_frame(lm_f, len))
        return 0;
    lm.frames++;
    lm.state_sig = sig;
    lm.state_at = now;
    lm.want_state = 0;
    return 1;
}
static int lm_job1(uint32_t now)            /* one frame owed to the host; 1 = one went */
{
    const lm_desc_t *d = lm_desc(mode);
    if (lm.err_n) {
        lm_m.cmd = R4P_LM_ERR;
        lm_m.u.lerr = lm.err[0];
        if (!lm_send(&lm_m))
            return 0;
        lm.err_n--;
        memmove(lm.err, lm.err + 1, lm.err_n * sizeof lm.err[0]);
        return 1;
    }
    if (lm.want_modes) {
        uint32_t i;
        memset(&lm_m.u.modes, 0, sizeof lm_m.u.modes);
        lm_m.cmd = R4P_MODES;
        lm_m.u.modes.cur = mode;
        lm_m.u.modes.n = LM_NMODES;
        for (i = 0; i < LM_NMODES; i++) {
            lm_m.u.modes.m[i].id = (uint8_t)i;
            lm_m.u.modes.m[i].flags = lm_mflags(i);
            lm_str(lm_m.u.modes.m[i].name, LM_MNAME[i], R4P_LM_MNAME);
        }
        if (!lm_send(&lm_m))
            return 0;
        lm.want_modes = 0;
        return 1;
    }
    if (lm.want_mode) {
        uint32_t h = lm_hash(mode);
        lm_m.cmd = R4P_MODE;
        lm_m.u.mode.mode = mode;
        lm_m.u.mode.flags = lm_mflags(mode);
        lm_m.u.mode.hash = h;
        if (!lm_send(&lm_m))
            return 0;
        lm.want_mode = 0;
        return 1;
    }
    if (!d || !lm.sub)
        return 0;
    if (lm.st != LMS_IDLE)
        return lm_stream(d);
    if (lm_values(d, now))
        return 1;
    return lm_state(d, now);
}
static void lm_jobs(uint32_t now)           /* (jobs_send, when nothing of 1.3 is owed) a few frames as there is room */
{
    uint32_t k;
    if (!host14())
        return;
    for (k = 0; k < 4u; k++)
        if (!lm_job1(now))
            break;
}
