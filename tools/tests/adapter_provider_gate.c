/*
 * The Blit display provider must preserve the Hibr console semantics the
 * desktop depends on. These are not visual preferences: pane ownership keeps
 * a wallpaper from erasing unchanged windows and the menu bar, watched file
 * descriptors wake application output without the desktop's 60-second idle
 * timer, and cell width/clipping determine where every widget lands.
 *
 * Include the production adapter so the assertions exercise the one shipped
 * implementation. The small Hibr stubs below are only the host ABI needed to
 * call it; no second adapter exists in this gate.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "hibr.h"

static char returned[256];
static int dial_count;

void lg(int level, const char *format, ...)
{
	(void)level;
	(void)format;
}

int hibr_dial(const char *host, const char *port, int udp)
{
	(void)host; (void)port; (void)udp;
	++dial_count;
	return -1;
}

const char *hibr_get(sh *s, const char *key)
{
	(void)s; (void)key;
	return NULL;
}

void hibr_ret(sh *s, const char *value)
{
	(void)s;
	snprintf(returned, sizeof(returned), "%s", value ? value : "");
}

int hibr_provide(sh *s, const char *name, unsigned ver, void *api)
{
	(void)s; (void)name; (void)ver; (void)api;
	return HIBR_OK;
}

int hibr_unprovide(sh *s, const char *name)
{
	(void)s; (void)name;
	return HIBR_OK;
}

void s_init(str *s) { s->p = NULL; s->n = 0; s->cap = 0; }
void s_free(str *s) { free(s->p); s_init(s); }

void s_add(str *s, const char *p, size_t n)
{
	char *grown = (char *)realloc(s->p, s->n + n + 1);
	if (grown == NULL) return;
	s->p = grown;
	memcpy(s->p + s->n, p, n);
	s->n += n;
	s->p[s->n] = '\0';
	s->cap = s->n + 1;
}

void s_cat(str *s, const char *p) { if (p != NULL) s_add(s, p, strlen(p)); }
void s_ch(str *s, int c) { char b = (char)c; s_add(s, &b, 1); }

void s_num(str *s, long value)
{
	char number[32];
	snprintf(number, sizeof(number), "%ld", value);
	s_cat(s, number);
}

int u8dec(const char *p, size_t n, unsigned *cp)
{
	const unsigned char *s = (const unsigned char *)p;
	if (!p || n == 0 || !cp) return 0;
	if (s[0] < 0x80) { *cp = s[0]; return 1; }
	if (n >= 2 && (s[0] & 0xe0) == 0xc0) {
		*cp = ((unsigned)(s[0] & 0x1f) << 6) | (s[1] & 0x3f); return 2;
	}
	if (n >= 3 && (s[0] & 0xf0) == 0xe0) {
		*cp = ((unsigned)(s[0] & 0x0f) << 12) |
			((unsigned)(s[1] & 0x3f) << 6) | (s[2] & 0x3f); return 3;
	}
	if (n >= 4 && (s[0] & 0xf8) == 0xf0) {
		*cp = ((unsigned)(s[0] & 7) << 18) |
			((unsigned)(s[1] & 0x3f) << 12) |
			((unsigned)(s[2] & 0x3f) << 6) | (s[3] & 0x3f); return 4;
	}
	*cp = 0xfffd; return 1;
}

int u8w(unsigned cp)
{
	if ((cp >= 0x300 && cp <= 0x36f) || cp == 0x200d) return 0;
	if (cp >= 0x1100 && (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
	    (cp >= 0x2e80 && cp <= 0xa4cf))) return 2;
	return 1;
}

#include "console-blit.c"

static int failures;

static void check(int condition, const char *what)
{
	if (condition) {
		printf("  ok   %s\n", what);
		return;
	}
	printf("  FAIL %s\n", what);
	++failures;
}

static void release_state(void)
{
	if (cb.ws_fd >= 0) close(cb.ws_fd);
	free(cb.pending);
	free(cb.shadow);
	free(cb.last_write);
	free(cb.ownership);
	memset(&cb, 0, sizeof(cb));
	cb.ws_fd = -1;
	memset(cb_panes, 0, sizeof(cb_panes));
	cb_pane_count = 0;
	cb_resize_pending = 0;
}

static void reset_state(int rows, int cols)
{
	release_state();
	cb.opened = 1;
	cb.rows = rows;
	cb.cols = cols;
	cb.pen_fg = DP_RGB | 0xffffffu;
	cb.pen_bg = DP_RGB | 0x000000u;
	check(cb_shadow_reset(), "provider state allocates its bounded cell maps");
}

static const cb_cell *at(int row, int col)
{
	return cb_effective_cell(col, row);
}

static int text_at(int row, int col, const char *text)
{
	const cb_cell *cell = at(row, col);
	return cell != NULL && strcmp(cell->text, text) == 0;
}

int main(void)
{
	cb_pane *pane;
	int sockets[2], watched_pipe[2];
	char *list_args[] = { "console", "pane", "list" };
	char *raise_args[] = { "console", "pane", "raise", "a" };
	char *hit_args[] = { "console", "hit", "1", "3" };
	char *raw_args[] = { "console", "put", "-r", "-p", "bar", "0", "0", "RAW" };
	char *resizing_args[] = { "console", "resizing" };

	memset(&cb, 0, sizeof(cb));
	cb.ws_fd = -1;

	reset_state(4, 12);
	dial_count = 0;
	check(cb_console(NULL, 2, resizing_args) == HIBR_FAIL && dial_count == 0,
	      "steady input checks resize state without an HTTP poll");
	pane = cb_set_pane("bar", 0, 0, 1, 8);
	check(pane != NULL, "a named pane is registered");
	cb_pane_fill(pane, 0, 0, 1, 8, " ");
	cb_pane_put(pane, 0, 0, "MENU");
	cb.behind = 1;
	cb_fill(0, 0, 4, 12, ".");
	cb.behind = 0;
	check(text_at(0, 0, "M") && text_at(0, 7, " "),
	      "wallpaper writes preserve every cell owned by the menu pane");
	check(text_at(1, 0, "."),
	      "the same wallpaper still paints outside owned panes");
	check(cb_drop_pane(pane), "dropping a pane succeeds once");
	cb.behind = 1;
	cb_fill(0, 0, 1, 8, ".");
	cb.behind = 0;
	check(text_at(0, 0, "."),
	      "a dropped pane releases its cells back to the wallpaper");

	reset_state(6, 12);
	pane = cb_set_pane("box", 2, 3, 2, 4);
	cb_pane_put(pane, 0, 0, "abcdef");
	check(text_at(2, 3, "a") && text_at(2, 6, "d") &&
	      !text_at(2, 7, "e"), "pane text is clipped to its own rectangle");
	cb_pane_fill(pane, 0, 2, 9, 9, "x");
	check(text_at(2, 5, "x") && text_at(3, 6, "x") &&
	      !text_at(4, 6, "x"), "pane fills are clipped in both axes");

	reset_state(4, 12);
	cb_set_pane("a", 1, 1, 2, 5);
	cb_set_pane("b", 1, 3, 2, 5);
	returned[0] = '\0'; cb_console(NULL, 3, list_args);
	check(strcmp(returned, "a b") == 0, "pane list is bottom-to-top");
	cb_console(NULL, 4, raise_args);
	returned[0] = '\0'; cb_console(NULL, 4, hit_args);
	check(strcmp(returned, "a") == 0, "raising a pane changes overlap hit order");

	reset_state(3, 12);
	cb_set_pane("bar", 0, 0, 1, 8);
	check(cb_console(NULL, 8, raw_args) == HIBR_OK && text_at(0, 0, "R"),
	      "raw paned put parses flags without turning -r into row zero");

	reset_state(3, 12);
	cb_put(0, 0, "漢a");
	check(text_at(0, 0, "漢") && text_at(0, 2, "a") &&
	      !text_at(0, 1, "a"), "wide UTF-8 advances by two logical cells");
	cb_put(1, 0, "e\314\201f");
	check(text_at(1, 0, "e\314\201") && text_at(1, 1, "f"),
	      "combining UTF-8 remains attached to its base cell");

	reset_state(3, 12);
	cb.pen_fg = DP_RGB | 0xc0c0c0u;
	cb.pen_bg = DP_RGB | 0x203040u;
	cb_put(1, 1, "x");
	cb_darken(1, 1, 1, 1, 50, 1);
	check(at(1, 1)->fg == (DP_RGB | 0x606060u) &&
	      at(1, 1)->bg == (DP_RGB | 0x101820u) &&
	      (at(1, 1)->attr & DP_DIM), "darken scales the effective cell style");
	{
		size_t pending = cb.pending_count;
		cb_darken(1, 1, 1, 1, 50, 1);
		check(cb.pending_count == pending,
		      "a shadow darkens the same unchanged cell only once");
	}

	reset_state(2, 8);
	check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
	      "the input stream fixture opens");
	check(pipe(watched_pipe) == 0, "the watched-descriptor fixture opens");
	cb.ws_fd = sockets[1];
	cb_watch_add(watched_pipe[0]);
	check(write(watched_pipe[1], "x", 1) == 1, "application output becomes ready");
	check(cb_wait_ready(50) == 2,
	      "watched application output wakes the desktop immediately");
	cb.quiet_wait = 1;
	check(cb_wait_ready(1) == 0,
	      "a quiet frame-cap wait deliberately ignores application output");
	close(sockets[0]);
	close(watched_pipe[0]);
	close(watched_pipe[1]);

	release_state();
	if (failures) {
		printf("ADAPTER_PROVIDER_FAIL failures=%d\n", failures);
		return 1;
	}
	puts("ADAPTER_PROVIDER_PASS pane_ownership=1 watched_wakeup=1 "
	     "utf8_width=1 resize_poll=0");
	return 0;
}
