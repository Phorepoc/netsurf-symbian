/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* phengine_ns.c -- the NetSurf web-engine plug-in core for the Phorepoc browser frontend (phebrowser).
 *
 * Implements phorepoc/phewebengine.h's PheWebEngineApi over NetSurf's core (the QuickJS-enabled tree): NetSurf content/layout/fetch (curl + mbedTLS) render
 * through libnsfb's "ram" surface (the framebuffer frontend's plotters + FreeType font handler) into a per-view bitmap the frontend
 * blits. NetSurf runs on ONE engine thread (a pthread); the seam calls (UI thread) only queue commands / read cached state.
 *
 * LICENSE: GPL-2.0-only (it links NetSurf, which is GPL). This file lives with the NetSurf port (netsurf-symbian), never in the Apache-2.0 core.
 * Resources (Messages, CSS, Choices, CA bundle) sit in \resource\phenetsurf\ of the install drive.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>

#include "utils/errors.h"
#include "utils/utils.h"
#include "utils/nsoption.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsurl.h"
#include "netsurf/browser_window.h"
#include "netsurf/keypress.h"
#include "netsurf/mouse.h"
#include "desktop/browser_history.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "netsurf/misc.h"
#include "netsurf/netsurf.h"
#include "content/fetch.h"

#include "framebuffer/framebuffer.h"
#include "framebuffer/schedule.h"
#include "framebuffer/findfile.h"
extern struct gui_layout_table *framebuffer_layout_table;
extern bool fb_font_init(void);
#include "framebuffer/clipboard.h"
#include "framebuffer/fetch.h"
#include "framebuffer/bitmap.h"

#include <phorepoc/phewebengine.h>

#define MAXV 8
#define RESDIR "/resource/phenetsurf/"
#define LOGFILE "C:\\data\\phenetsurf.log"

extern void phns_log(const char *msg);            /* RDebug (phewebengine_netsurf.cpp) */
extern char **respaths;
extern struct gui_utf8_table *framebuffer_utf8_table;

/* the engine's per-view state; also NetSurf's struct gui_window */
struct gui_window {
	int used;
	PheView id;
	struct browser_window *bw;
	nsfb_t *nsfb;
	int w, h, sx, sy, cw, ch;
	int loading, progress, sec, can_back, can_fwd, focus, dirty;
	const void *fowner;                            /* the focused field (patch 0114), to see a field-to-field move */
	int down;
	char url[1024], title[256], status[256], err[128];
	char fval[1024];                               /* PHE_STR_FOCUS_VALUE: the focused text field (never a password) */
};

static struct gui_window views[MAXV];
static struct gui_window *creating;               /* the view whose navigation is creating its browser_window right now */
static PheNotify notify_fn;
static void *notify_ctx;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;      /* events, queue, cached state */
static pthread_mutex_t pixmu = PTHREAD_MUTEX_INITIALIZER;   /* pixel buffers */
static pthread_t thr;
static volatile int quitting, ready, initerr;
static char iapname[32];
static PheView nextid = 1;

/* ---- events ----------------------------------------------------------------------------------------------------------- */
static PheEvent evq[256];
static int evh, evt;

static void post(struct gui_window *g, int kind, int a, int b)
{
	int merged = 0;
	pthread_mutex_lock(&mu);
	if (kind == PHE_EV_REPAINT || kind == PHE_EV_PROGRESS) {      /* coalesce */
		int i;
		for (i = evh; i != evt; i = (i + 1) & 255)
			if (evq[i].kind == kind && evq[i].view == g->id) { evq[i].a = a; evq[i].b = b; merged = 1; break; }
	}
	if (!merged && ((evt + 1) & 255) != evh) {
		evq[evt].kind = kind; evq[evt].view = g->id; evq[evt].a = a; evq[evt].b = b; evq[evt].id = 0;
		evt = (evt + 1) & 255;
	}
	pthread_mutex_unlock(&mu);
	if (notify_fn)
		notify_fn(notify_ctx, g->id);
}

static void trace(const char *a, const char *b)
{
	char buf[300];
	snprintf(buf, sizeof buf, "%s %s", a, b ? b : "");
	phns_log(buf);
}

/* ---- UTF-16 <-> UTF-8 -------------------------------------------------------------------------------------------------- */
static char *u16to8(const unsigned short *s, int len)
{
	char *o = malloc((size_t)len * 3 + 1), *p = o;
	int i;
	if (!o) return NULL;
	for (i = 0; i < len; i++) {
		unsigned c = s[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++;
		}
		if (c < 0x80) *p++ = (char)c;
		else if (c < 0x800) { *p++ = (char)(0xC0 | (c >> 6)); *p++ = (char)(0x80 | (c & 63)); }
		else if (c < 0x10000) { *p++ = (char)(0xE0 | (c >> 12)); *p++ = (char)(0x80 | ((c >> 6) & 63)); *p++ = (char)(0x80 | (c & 63)); }
		else { *p++ = (char)(0xF0 | (c >> 18)); *p++ = (char)(0x80 | ((c >> 12) & 63)); *p++ = (char)(0x80 | ((c >> 6) & 63)); *p++ = (char)(0x80 | (c & 63)); }
	}
	*p = 0;
	return o;
}

/* next code point of a UTF-8 string; advances *pp */
static unsigned next_cp(const unsigned char **pp)
{
	const unsigned char *p = *pp;
	unsigned c = *p++;
	if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
	else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
	else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
	*pp = p;
	return c;
}

static int u8to16(const char *s, unsigned short *out, int cap)   /* returns the full length in UTF-16 units */
{
	const unsigned char *p = (const unsigned char *)s;
	int n = 0;
	while (*p) {
		unsigned c = next_cp(&p);
		if (c >= 0x10000) {
			c -= 0x10000;
			if (out && n + 1 < cap) { out[n] = (unsigned short)(0xD800 + (c >> 10)); out[n + 1] = (unsigned short)(0xDC00 + (c & 1023)); }
			n += 2;
		} else {
			if (out && n < cap) out[n] = (unsigned short)c;
			n++;
		}
	}
	return n;
}

/* ---- command queue (UI thread -> engine thread) -------------------------------------------------------------------------- */
enum { C_DESTROY, C_RESIZE, C_LOAD, C_CMD, C_SCROLL, C_PTR, C_TEXT, C_KEY, C_ZOOM, C_SETFIELD };
struct cmd { struct cmd *next; int type; PheView v; int a, b, c; char *s; };
static struct cmd *qhead, *qtail;

static void enqueue(int type, PheView v, int a, int b, int c, char *s)
{
	struct cmd *k = calloc(1, sizeof *k);
	if (!k) { free(s); return; }
	k->type = type; k->v = v; k->a = a; k->b = b; k->c = c; k->s = s;
	pthread_mutex_lock(&mu);
	if (qtail) qtail->next = k; else qhead = k;
	qtail = k;
	pthread_mutex_unlock(&mu);
}

static struct gui_window *find(PheView v)
{
	int i;
	for (i = 0; i < MAXV; i++)
		if (views[i].used && views[i].id == v) return &views[i];
	return NULL;
}

/* ---- drawing ---------------------------------------------------------------------------------------------------------- */
static void clamp_scroll(struct gui_window *g)
{
	int mx = g->cw - g->w, my = g->ch - g->h;
	if (mx < 0) mx = 0;
	if (my < 0) my = 0;
	if (g->sx > mx) g->sx = mx;
	if (g->sy > my) g->sy = my;
	if (g->sx < 0) g->sx = 0;
	if (g->sy < 0) g->sy = 0;
}

static void update_extent(struct gui_window *g)
{
	int w = 0, h = 0;
	if (g->bw && browser_window_get_extents(g->bw, false, &w, &h) == NSERROR_OK) { g->cw = w; g->ch = h; }
	clamp_scroll(g);
}

static void redraw(struct gui_window *g)
{
	struct redraw_context ctx;
	struct rect clip;
	nsfb_bbox_t box;
	uint8_t *ptr; int ll, y, x;
	g->dirty = 0;
	if (!g->nsfb) return;
	memset(&ctx, 0, sizeof ctx);
	ctx.interactive = true; ctx.background_images = true; ctx.plot = &fb_plotters;
	clip.x0 = 0; clip.y0 = 0; clip.x1 = g->w; clip.y1 = g->h;
	box.x0 = 0; box.y0 = 0; box.x1 = g->w; box.y1 = g->h;
	pthread_mutex_lock(&pixmu);
	framebuffer_set_surface(g->nsfb);
	nsfb_claim(g->nsfb, &box);
	nsfb_plot_rectangle_fill(g->nsfb, &box, 0xFFFFFFFFu);
	if (g->bw)
		browser_window_redraw(g->bw, -g->sx, -g->sy, &clip, &ctx);
	nsfb_update(g->nsfb, &box);
	if (nsfb_get_buffer(g->nsfb, &ptr, &ll) == 0 && ptr)
		for (y = 0; y < g->h; y++) {
			uint32_t *row = (uint32_t *)(ptr + (size_t)y * ll);
			for (x = 0; x < g->w; x++) row[x] |= 0xFF000000u;
		}
	pthread_mutex_unlock(&pixmu);
	post(g, PHE_EV_REPAINT, 0, 0);
}

static void refresh_state(struct gui_window *g)
{
	if (g->bw) {
		g->can_back = browser_window_history_back_available(g->bw);
		g->can_fwd = browser_window_history_forward_available(g->bw);
	}
}

/* ---- NetSurf gui_window_table -------------------------------------------------------------------------------------------- */
static struct gui_window *gw_create(struct browser_window *bw, struct gui_window *existing, gui_window_create_flags flags)
{
	(void)existing; (void)flags;
	if (!creating) return NULL;                    /* a page-opened window: not supported yet */
	creating->bw = bw;
	return creating;
}
static void gw_destroy(struct gui_window *gw) { gw->bw = NULL; }
static nserror gw_invalidate(struct gui_window *gw, const struct rect *r) { (void)r; gw->dirty = 1; return NSERROR_OK; }
static bool gw_get_scroll(struct gui_window *gw, int *sx, int *sy) { *sx = gw->sx; *sy = gw->sy; return true; }
static nserror gw_set_scroll(struct gui_window *gw, const struct rect *r)
{
	gw->sx = r->x0; gw->sy = r->y0; clamp_scroll(gw); gw->dirty = 1; return NSERROR_OK;
}
static nserror gw_get_dimensions(struct gui_window *gw, int *w, int *h) { *w = gw->w; *h = gw->h; return NSERROR_OK; }
static nserror gw_event(struct gui_window *gw, enum gui_window_event ev)
{
	switch (ev) {
	case GW_EVENT_UPDATE_EXTENT: update_extent(gw); break;
	case GW_EVENT_REMOVE_CARET:
		if (gw->focus) { gw->focus = 0; post(gw, PHE_EV_FOCUS, 0, 0); }   /* fowner stays: NetSurf removes the caret after each press */
		break;
	case GW_EVENT_START_THROBBER:
		pthread_mutex_lock(&mu); gw->loading = 1; gw->progress = 10; pthread_mutex_unlock(&mu);
		post(gw, PHE_EV_START, 0, 0);
		post(gw, PHE_EV_PROGRESS, 10, 0);
		break;
	case GW_EVENT_STOP_THROBBER:
		update_extent(gw);
		pthread_mutex_lock(&mu); gw->loading = 0; gw->progress = 100; pthread_mutex_unlock(&mu);
		gw->dirty = 1;
		refresh_state(gw);
		post(gw, PHE_EV_HISTORY, 0, 0);
		post(gw, PHE_EV_FINISHED, 0, 0);
		break;
	default: break;
	}
	return NSERROR_OK;
}
static void gw_set_title(struct gui_window *gw, const char *t)
{
	pthread_mutex_lock(&mu); strncpy(gw->title, t ? t : "", sizeof gw->title - 1); gw->title[sizeof gw->title - 1] = 0; pthread_mutex_unlock(&mu);
	post(gw, PHE_EV_TITLE, 0, 0);
}
static nserror gw_set_url(struct gui_window *gw, struct nsurl *url)
{
	const char *s = nsurl_access(url);
	pthread_mutex_lock(&mu);
	strncpy(gw->url, s, sizeof gw->url - 1); gw->url[sizeof gw->url - 1] = 0;
	gw->sec = (strncmp(s, "https:", 6) == 0) ? PHE_SEC_SECURE : PHE_SEC_NONE;
	gw->fowner = NULL;                                 /* a new document: no field has the focus */
	gw->title[0] = 0;
	pthread_mutex_unlock(&mu);
	refresh_state(gw);
	post(gw, PHE_EV_COMMIT, 0, gw->sec);
	return NSERROR_OK;
}
static void gw_set_status(struct gui_window *gw, const char *t)
{
	pthread_mutex_lock(&mu);
	strncpy(gw->status, t ? t : "", sizeof gw->status - 1);
	if (gw->loading && gw->progress < 60) gw->progress = 60;
	pthread_mutex_unlock(&mu);
	if (gw->loading) post(gw, PHE_EV_PROGRESS, 60, 0);
}
static void gw_set_pointer(struct gui_window *gw, enum gui_pointer_shape s) { (void)gw; (void)s; }
/* the focused text field's value (engine thread): the frontend's editor starts with it (PHE_STR_FOCUS_VALUE).
 * A password field gives "" (its text is never shown in clear). */
int phns_focus_value(struct hlcache_handle *h, char *buf, unsigned int len);   /* patch 0110 (html/interaction.c) */
const void *phns_focus_owner(struct hlcache_handle *h, int *password);         /* patch 0114 */
static void capture_focus_value(struct gui_window *gw)
{
	char tmp[sizeof gw->fval];
	if (!gw->bw || !phns_focus_value(browser_window_get_content(gw->bw), tmp, sizeof tmp))
		tmp[0] = 0;
	tmp[sizeof tmp - 1] = 0;
	pthread_mutex_lock(&mu); memcpy(gw->fval, tmp, sizeof tmp); pthread_mutex_unlock(&mu);
}
static void gw_place_caret(struct gui_window *gw, int x, int y, int h, const struct rect *clip)
{
	(void)x; (void)y; (void)h; (void)clip;
	int pw = 0;
	const void *owner = gw->bw ? phns_focus_owner(browser_window_get_content(gw->bw), &pw) : NULL;
	if (owner == gw->fowner) {                         /* the same field: the caret moved, or the frontend's commit
	                                                    * (set_field) put it back; a = 2 alone, which stays muted */
		if (!gw->focus) { gw->focus = 1; capture_focus_value(gw); post(gw, PHE_EV_FOCUS, 2, pw); }
		return;
	}
	post(gw, PHE_EV_FOCUS, 0, 0);                      /* another field: the old editor closes first (and the
	                                                    * frontend's post-commit mute lifts) */
	gw->fowner = owner;
	capture_focus_value(gw);
	gw->focus = 1;
	post(gw, PHE_EV_FOCUS, 2, pw);                     /* b = 1: a password field (a masked editor) */
}

static struct gui_window_table window_table = {
	.create = gw_create, .destroy = gw_destroy, .invalidate = gw_invalidate, .get_scroll = gw_get_scroll,
	.set_scroll = gw_set_scroll, .get_dimensions = gw_get_dimensions, .event = gw_event, .set_title = gw_set_title,
	.set_url = gw_set_url, .set_status = gw_set_status, .set_pointer = gw_set_pointer, .place_caret = gw_place_caret,
};

static nserror ns_schedule(int tival, void (*cb)(void *p), void *p) { return framebuffer_schedule(tival, cb, p); }
static void ns_quit(void) { }
static struct gui_misc_table misc_table = { .schedule = ns_schedule, .quit = ns_quit };

static nserror set_defaults(struct nsoption_s *defaults)
{
	nsoption_setnull_charp(cookie_file, strdup("C:/data/phenetsurf_cookies"));
	nsoption_setnull_charp(cookie_jar, strdup("C:/data/phenetsurf_cookies"));
#ifdef PHE_NS_QJS
#include "colours_qjs.inc"
#else
#include "colours.inc"
#endif
	return NSERROR_OK;
}

static bool nslog_stream_configure(FILE *f) { setbuf(f, NULL); return true; }

/* ---- engine thread: init -------------------------------------------------------------------------------------------------- */
static char resroot[8];

static int find_resources(void)
{
	static const char drives[] = "EFCDGHZ";   /* installed packages sit on E/F/C/D (Z: last) */
	int i;
	for (i = 0; drives[i]; i++) {
		char p[64];
		FILE *f;
		snprintf(p, sizeof p, "%c:\\resource\\phenetsurf\\Messages", drives[i]);
		f = fopen(p, "r");
		if (f) { fclose(f); snprintf(resroot, sizeof resroot, "%c:\\", drives[i]); return 0; }
	}
	return -1;
}

extern void phns_hold_connection(const char *iap);   /* phewebengine_netsurf.cpp: keeps the access point up */
extern void ram_register_surface(void);      /* libnsfb: only a constructor references it, so the archive member needs a real reference */
static int ns_start(void)
{
	static struct netsurf_table tbl;          /* static: NetSurf keeps the POINTER (guit = &tbl), not a copy -- a local dangled once ns_start returned */
	char *opts, *msgs;
	int argc = 1;
	char *argv[2] = { "phenetsurf", NULL };
	struct ifreq ifr;
	FILE *f;
	memset(&tbl, 0, sizeof tbl);
	tbl.misc = &misc_table; tbl.window = &window_table; tbl.clipboard = framebuffer_clipboard_table;
	tbl.fetch = framebuffer_fetch_table; tbl.utf8 = framebuffer_utf8_table; tbl.bitmap = framebuffer_bitmap_table;
	tbl.layout = framebuffer_layout_table;
	if (nsfb_type_from_name("ram") == NSFB_SURFACE_NONE) { trace("ns: ram surface not registered by a constructor: registering", NULL); ram_register_surface(); }
	if (find_resources() != 0) { trace("ns: no resources", NULL); return -1; }
	chdir(resroot);
	freopen(LOGFILE, "w", stderr);
	freopen("C:\\data\\phenetsurf.out", "w", stdout);
	setvbuf(stderr, NULL, _IONBF, 0);
	f = fopen("C:\\data\\netsurf_iap.txt", "r");
	memset(&ifr, 0, sizeof ifr);
	if (iapname[0]) strncpy(ifr.ifr_name, iapname, sizeof ifr.ifr_name - 1);
	else if (f == NULL || fgets(ifr.ifr_name, sizeof ifr.ifr_name, f) == NULL) strncpy(ifr.ifr_name, "ASUS", sizeof ifr.ifr_name - 1);
	if (f) fclose(f);
	ifr.ifr_name[strcspn(ifr.ifr_name, "\r\n")] = 0;
	trace("ns: setdefaultif", ifr.ifr_name);
	setdefaultif(&ifr);
	phns_hold_connection(ifr.ifr_name);
	if (netsurf_register(&tbl) != NSERROR_OK) { trace("ns: register failed", NULL); return -1; }
	respaths = fb_init_resource_path(RESDIR);
	nslog_init(nslog_stream_configure, &argc, argv);
	if (nsoption_init(set_defaults, &nsoptions, &nsoptions_default) != NSERROR_OK) { trace("ns: options failed", NULL); return -1; }
	opts = filepath_find(respaths, "Choices");
	nsoption_read(opts, nsoptions);
	free(opts);
	msgs = filepath_find(respaths, "Messages");
	if (messages_add_from_file(msgs) != NSERROR_OK) trace("ns: messages failed", NULL);
	free(msgs);
	if (netsurf_init(NULL) != NSERROR_OK) { trace("ns: netsurf_init failed", NULL); return -1; }
	nsoption_set_bool(core_select_menu, true);
	if (!fb_font_init()) { trace("ns: fonts failed", NULL); return -1; }
	trace("ns: ready", resroot);
	return 0;
}

/* ---- engine thread: commands ---------------------------------------------------------------------------------------------- */
static void do_load(struct gui_window *g, const char *url)
{
	nsurl *u;
	nserror e = nsurl_create(url, &u);
	if (e != NSERROR_OK) { trace("ns: bad url", url); return; }
	if (!g->bw) {
		creating = g;
		e = browser_window_create(BW_CREATE_HISTORY, u, NULL, NULL, &g->bw);
		creating = NULL;
		/* (no browser_window_set_dimensions: that is for core-managed windows only and asserts for a front-end one;
		   the core asks gw_get_dimensions) */
	} else {
		e = browser_window_navigate(g->bw, u, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
	}
	nsurl_unref(u);
	trace(e == NSERROR_OK ? "ns: load" : "ns: load FAILED", url);
}

static void do_cmd(struct cmd *k)
{
	struct gui_window *g = find(k->v);
	if (!g) return;
	switch (k->type) {
	case C_DESTROY:
		if (g->bw) { struct browser_window *bw = g->bw; g->bw = NULL; browser_window_destroy(bw); }
		pthread_mutex_lock(&pixmu); if (g->nsfb) { nsfb_free(g->nsfb); g->nsfb = NULL; } pthread_mutex_unlock(&pixmu);
		pthread_mutex_lock(&mu); g->used = 0; pthread_mutex_unlock(&mu);
		break;
	case C_RESIZE:
		pthread_mutex_lock(&pixmu);
		framebuffer_set_surface(g->nsfb);
		nsfb_set_geometry(g->nsfb, k->a, k->b, NSFB_FMT_XRGB8888);
		g->w = k->a; g->h = k->b;
		pthread_mutex_unlock(&pixmu);
		if (g->bw) browser_window_schedule_reformat(g->bw);     /* the core re-reads gw_get_dimensions */
		update_extent(g); g->dirty = 1;
		break;
	case C_LOAD: if (k->s) do_load(g, k->s); break;
	case C_CMD:
		if (!g->bw) break;
		switch (k->a) {
		case PHE_CMD_STOP: browser_window_stop(g->bw); break;
		case PHE_CMD_RELOAD: case PHE_CMD_RELOAD_BYPASS_CACHE: browser_window_reload(g->bw, true); break;
		case PHE_CMD_BACK: if (browser_window_history_back_available(g->bw)) browser_window_history_back(g->bw, false); break;
		case PHE_CMD_FORWARD: if (browser_window_history_forward_available(g->bw)) browser_window_history_forward(g->bw, false); break;
		}
		refresh_state(g);
		break;
	case C_SCROLL:
		g->sx += k->a; g->sy += k->b; clamp_scroll(g); g->dirty = 1; break;
	case C_PTR:
		if (!g->bw) break;
		if (k->a == PHE_PTR_DOWN) {
			g->down = 1;
			browser_window_mouse_track(g->bw, BROWSER_MOUSE_HOVER, k->b + g->sx, k->c + g->sy);
			browser_window_mouse_click(g->bw, BROWSER_MOUSE_PRESS_1, k->b + g->sx, k->c + g->sy);
		} else if (k->a == PHE_PTR_UP) {
			if (g->down) browser_window_mouse_click(g->bw, BROWSER_MOUSE_CLICK_1, k->b + g->sx, k->c + g->sy);
			g->down = 0;
		} else if (k->a == PHE_PTR_MOVE) {
			browser_window_mouse_track(g->bw, BROWSER_MOUSE_HOVER, k->b + g->sx, k->c + g->sy);
		}
		g->dirty = 1;
		break;
	case C_KEY:
		if (g->bw) browser_window_key_press(g->bw, (uint32_t)k->a);
		g->dirty = 1;
		break;
	case C_ZOOM:                                       /* a = permille; the page reflows at the new scale */
		if (g->bw && k->a >= 250 && k->a <= 5000) browser_window_set_scale(g->bw, (float)k->a / 1000.0f, true);
		g->dirty = 1;
		break;
	case C_SETFIELD:                                   /* replace the focused field's text: select all, delete, type */
		if (g->bw) {
			browser_window_key_press(g->bw, NS_KEY_SELECT_ALL);
			browser_window_key_press(g->bw, NS_KEY_DELETE_LEFT);
		}
		/* fall through */
	case C_TEXT:
		if (g->bw && k->s) {
			const unsigned char *p = (const unsigned char *)k->s;
			while (*p) browser_window_key_press(g->bw, next_cp(&p));
		}
		g->dirty = 1;
		break;
	}
}

void phns_install_fault_logger(void);
void phns_heap_tick(int live, int sessions, int keys, int keypool);   /* phewebengine_netsurf.cpp: bounded heap log line */
extern int phns_tls_live, phns_tls_sessions;  /* content/fetchers/curl.c (patch 0106) */
int phns_psa_keys(int *pool);                  /* content/fetchers/curl.c (patch 0106) */
static void *engine_main(void *arg)
{
	(void)arg;
	phns_install_fault_logger();
	initerr = ns_start();
	ready = 1;
	while (!quitting && initerr == 0) {
		struct cmd *k;
		int t, i, busy = 0;
		for (;;) {
			pthread_mutex_lock(&mu);
			k = qhead;
			if (k) { qhead = k->next; if (!qhead) qtail = NULL; }
			pthread_mutex_unlock(&mu);
			if (!k) break;
			do_cmd(k);
			free(k->s); free(k);
		}
		t = schedule_run();
		{ int pool, keys = phns_psa_keys(&pool); phns_heap_tick(phns_tls_live, phns_tls_sessions, keys, pool); }
		for (i = 0; i < MAXV; i++)
			if (views[i].used) {
				if (views[i].loading) busy = 1;
				if (views[i].dirty && views[i].nsfb) { update_extent(&views[i]); redraw(&views[i]); }
			}
		if (t < 0 || t > (busy ? 15 : 40)) t = busy ? 15 : 40;
		if (t < 2) t = 2;
		usleep((useconds_t)t * 1000);
	}
	trace("ns: engine thread exits", NULL);
	return NULL;
}

/* ---- the seam (UI thread) ------------------------------------------------------------------------------------------------- */
static int e_init(PheNotify n, void *ctx)
{
	pthread_attr_t at;
	int i;
	notify_fn = n; notify_ctx = ctx;
	if (ready) return 0;
	pthread_attr_init(&at);
#ifdef PHE_NS_QJS
	pthread_attr_setstacksize(&at, 1024 * 1024);   /* QuickJS (limit 256 KB, patch 0103) + deep layout recursion */
#else
	pthread_attr_setstacksize(&at, 256 * 1024);
#endif
	if (pthread_create(&thr, &at, engine_main, NULL) != 0) return -1;
	{	/* wait for NetSurf init by the wall clock, up to 20 s: on a device usleep(10000) returns at once, so a
		   count of sleeps ran out in ~30 ms while QuickJS + NetSurf were still starting (e_init FAIL, nothing loads) */
		struct timeval t0, t;
		gettimeofday(&t0, NULL);
		for (i = 0; !ready; i++) {
			usleep(10000);
			gettimeofday(&t, NULL);
			if ((t.tv_sec - t0.tv_sec) * 1000000L + (t.tv_usec - t0.tv_usec) > 20000000L) break;
		}
	}
	trace("ns: e_init returns", ready && !initerr ? "0" : "FAIL");
	return (ready && !initerr) ? 0 : -2;
}
static void e_shutdown(void) { quitting = 1; pthread_join(thr, NULL); ready = 0; }
static void e_set_iap(const char *name) { strncpy(iapname, name ? name : "", sizeof iapname - 1); }
static void e_memory_pressure(int level) { (void)level; }
static void e_set_ua(const unsigned short *s, int len) { (void)s; (void)len; }
static PheView e_new_view_id(void) { return nextid++; }

static void e_view_create(PheView v, int w, int h, int priv, const unsigned char *st, int stlen)
{
	int i;
	(void)priv; (void)st; (void)stlen;
	trace("ns: e_view_create", NULL);
	if (w < 64) w = 64;
	if (h < 64) h = 64;
	pthread_mutex_lock(&pixmu);
	pthread_mutex_lock(&mu);
	for (i = 0; i < MAXV && views[i].used; i++) ;
	if (i == MAXV) { pthread_mutex_unlock(&mu); pthread_mutex_unlock(&pixmu); return; }
	memset(&views[i], 0, sizeof views[i]);
	views[i].id = v; views[i].w = w; views[i].h = h;
	pthread_mutex_unlock(&mu);
	views[i].nsfb = framebuffer_initialise("ram", w, h, 32);
	views[i].used = views[i].nsfb != NULL;
	trace(views[i].nsfb ? "ns: view created" : "ns: view create FAILED (no ram surface)", NULL);
	pthread_mutex_unlock(&pixmu);
}
static void e_view_destroy(PheView v) { enqueue(C_DESTROY, v, 0, 0, 0, NULL); }
static void e_view_resize(PheView v, int w, int h) { if (w > 0 && h > 0) enqueue(C_RESIZE, v, w, h, 0, NULL); }
static void e_view_visible(PheView v, int vis) { (void)v; (void)vis; }
static void e_view_suspend(PheView v) { (void)v; }
static int e_view_save_state(PheView v, unsigned char *b, int cap) { (void)v; (void)b; (void)cap; return 0; }
static void e_load(PheView v, const unsigned short *url, int len) { enqueue(C_LOAD, v, 0, 0, 0, u16to8(url, len)); }
static void e_cmd(PheView v, int c) { enqueue(C_CMD, v, c, 0, 0, NULL); }
static void e_get_state(PheView v, PheViewState *o)
{
	struct gui_window *g;
	memset(o, 0, sizeof *o);
	o->zoom_permille = 1000;
	{ struct gui_window *zg = find(v); if (zg && zg->bw) o->zoom_permille = (int)(browser_window_get_scale(zg->bw) * 1000.0f + 0.5f); }
	pthread_mutex_lock(&mu);
	g = find(v);
	if (g) {
		o->can_back = g->can_back; o->can_forward = g->can_fwd; o->loading = g->loading; o->progress = g->progress;
		o->security = g->sec; o->scroll_x = g->sx; o->scroll_y = g->sy; o->content_w = g->cw; o->content_h = g->ch;
	}
	pthread_mutex_unlock(&mu);
}
static int e_get_str(PheView v, int which, unsigned short *buf, int cap)
{
	struct gui_window *g;
	char tmp[1100];
	tmp[0] = 0;
	pthread_mutex_lock(&mu);
	g = find(v);
	if (g) {
		const char *s = which == PHE_STR_URL ? g->url : which == PHE_STR_TITLE ? g->title : which == PHE_STR_ERROR ? g->err :
		                which == PHE_STR_FOCUS_VALUE ? g->fval : "";
		strncpy(tmp, s, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
	}
	pthread_mutex_unlock(&mu);
	return u8to16(tmp, buf, cap);
}
static int e_poll(PheView v, PheEvent *o)
{
	int r = 0, i;
	pthread_mutex_lock(&mu);
	for (i = evh; i != evt; i = (i + 1) & 255)
		if (v == 0 || evq[i].view == v) {
			*o = evq[i];
			while (i != evh) { int p = (i + 255) & 255; evq[i] = evq[p]; i = p; }   /* close the gap */
			evh = (evh + 1) & 255;
			r = 1;
			break;
		}
	pthread_mutex_unlock(&mu);
	return r;
}
static void e_dialog_reply(PheView v, unsigned id, int ok, const unsigned short *a, int al, const unsigned short *b, int bl)
{ (void)v; (void)id; (void)ok; (void)a; (void)al; (void)b; (void)bl; }
static void e_scroll(PheView v, int dx, int dy) { enqueue(C_SCROLL, v, dx, dy, 0, NULL); }
static void e_zoom(PheView v, int z, int cx, int cy) { (void)cx; (void)cy; enqueue(C_ZOOM, v, z, 0, 0, NULL); }
static void e_pointer(PheView v, int type, int x, int y) { enqueue(C_PTR, v, type, x, y, NULL); }
static void e_key(PheView v, int scan, int uc, int down) { (void)scan; if (down && uc > 0) enqueue(C_KEY, v, uc, 0, 0, NULL); }
static void e_text(PheView v, const unsigned short *s, int len) { enqueue(C_TEXT, v, 0, 0, 0, u16to8(s, len)); }
static void e_set_field(PheView v, const unsigned short *s, int len) { enqueue(C_SETFIELD, v, 0, 0, 0, u16to8(s, len)); }
static void e_setting(PheView v, int id, int val) { (void)v; (void)id; (void)val; }
static int e_lock_pixels(PheView v, const unsigned int **px, int *w, int *h, int *stride)
{
	struct gui_window *g;
	uint8_t *ptr; int ll;
	pthread_mutex_lock(&pixmu);
	g = find(v);
	if (!g || !g->nsfb || nsfb_get_buffer(g->nsfb, &ptr, &ll) != 0 || !ptr) { pthread_mutex_unlock(&pixmu); return -1; }
	*px = (const unsigned int *)ptr; *w = g->w; *h = g->h; *stride = ll;
	return 0;                                          /* pixmu stays held until unlock_pixels */
}
static void e_unlock_pixels(PheView v) { (void)v; pthread_mutex_unlock(&pixmu); }
static void e_clear_data(int bits) { (void)bits; }

static const PheWebEngineApi api = {
	sizeof(PheWebEngineApi), PHE_WEBENGINE_VERSION, "NetSurf", "3.11", PHE_CAP_REFLOW | PHE_CAP_ZOOM, MAXV,
	e_init, e_shutdown, e_set_ua, e_set_iap, e_memory_pressure,
	e_new_view_id, e_view_create, e_view_destroy, e_view_resize, e_view_visible, e_view_suspend, e_view_save_state,
	e_load, e_cmd, e_get_state, e_get_str, e_poll, e_dialog_reply,
	e_scroll, e_zoom, e_pointer, e_key, e_text, e_setting,
	e_lock_pixels, e_unlock_pixels, NULL,
	NULL, NULL, NULL, NULL, NULL, e_clear_data,
	e_set_field
};

const PheWebEngineApi *PheNetSurfGetApi(unsigned wanted)
{
	return wanted == PHE_WEBENGINE_VERSION ? &api : NULL;
}
