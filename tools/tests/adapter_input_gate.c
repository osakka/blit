/*
 * Issues #76 and #77. Exercise the input semantics owned by console-blit.
 *
 * The server's stream reports terminal cells one-based. Hibr's console API
 * reports every row and column zero-based. The adapter is the one boundary
 * between those contracts, so it must translate exactly once. It also owns
 * the evdev-code to Hibr-key-name vocabulary used by a provider-backed
 * desktop. This harness includes the shipped adapter and feeds it real
 * unmasked WebSocket text frames, without inventing a second input path.
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

/* --- the Hibr runtime, reduced to what the adapter links against -------- */

void lg(int level, const char *format, ...)
{
	(void)level;
	(void)format;
}

int hibr_dial(const char *host, const char *port, int udp)
{
	(void)host;
	(void)port;
	(void)udp;
	return -1;
}

const char *hibr_get(sh *s, const char *key)
{
	(void)s;
	(void)key;
	return NULL;
}

void hibr_ret(sh *s, const char *value) { (void)s; (void)value; }

int hibr_provide(sh *s, const char *name, unsigned ver, void *api)
{
	(void)s;
	(void)name;
	(void)ver;
	(void)api;
	return HIBR_OK;
}

int hibr_unprovide(sh *s, const char *name)
{
	(void)s;
	(void)name;
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

/* --- the gate ---------------------------------------------------------- */

static int failures;

static int write_all(int fd, const void *data, size_t length)
{
	const unsigned char *bytes = (const unsigned char *)data;
	size_t written = 0;
	while (written < length) {
		ssize_t count = write(fd, bytes + written, length - written);
		if (count <= 0) return 0;
		written += (size_t)count;
	}
	return 1;
}

static int send_text_frame(int fd, const char *json)
{
	unsigned char header[4];
	size_t length = strlen(json);
	size_t header_length;

	header[0] = 0x81;
	if (length <= 125) {
		header[1] = (unsigned char)length;
		header_length = 2;
	} else if (length <= 65535) {
		header[1] = 126;
		header[2] = (unsigned char)(length >> 8);
		header[3] = (unsigned char)length;
		header_length = 4;
	} else {
		return 0;
	}
	return write_all(fd, header, header_length) &&
		write_all(fd, json, length);
}

static void expect_text(const char *what, const char *got, const char *want)
{
	if (got != NULL && strcmp(got, want) == 0) {
		printf("  ok   %-42s %s\n", what, got);
		return;
	}
	printf("  FAIL %-42s got=%s expected=%s\n", what,
		got != NULL ? got : "(null)", want);
	++failures;
}

static void expect_frame_metrics(const char *what, const char *json,
			 int mouse_mode, int cell_width, int cell_height,
			 const char *want)
{
	int sockets[2];
	int result;
	str output;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		perror("socketpair");
		exit(2);
	}
	memset(&cb, 0, sizeof(cb));
	cb.opened = 1;
	cb.ws_fd = sockets[1];
	cb.mouse_mode = mouse_mode;
	cb.cell_width = cell_width;
	cb.cell_height = cell_height;
	snprintf(cb.session, sizeof(cb.session), "session-4");
	if (!send_text_frame(sockets[0], json)) {
		perror("write WebSocket frame");
		exit(2);
	}
	s_init(&output);
	result = cb_key(100, &output);
	if (result != 1) {
		printf("  FAIL %-42s result=%d expected=1\n", what, result);
		++failures;
	} else {
		expect_text(what, output.p, want);
	}
	s_free(&output);
	close(sockets[0]);
	close(sockets[1]);
}

static void expect_frame(const char *what, const char *json, int mouse_mode,
			 const char *want)
{
	expect_frame_metrics(what, json, mouse_mode, 8, 16, want);
}

static void expect_drag_frame(const char *what, const char *json,
			      const char *want)
{
	int sockets[2];
	int result;
	str output;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		perror("socketpair");
		exit(2);
	}
	memset(&cb, 0, sizeof(cb));
	cb.opened = 1;
	cb.ws_fd = sockets[1];
	cb.mouse_mode = 2;
	cb.mouse_buttons = 1;
	snprintf(cb.session, sizeof(cb.session), "session-4");
	if (!send_text_frame(sockets[0], json)) {
		perror("write WebSocket frame");
		exit(2);
	}
	s_init(&output);
	result = cb_key(100, &output);
	if (result != 1) {
		printf("  FAIL %-42s result=%d expected=1\n", what, result);
		++failures;
	} else {
		expect_text(what, output.p, want);
	}
	s_free(&output);
	close(sockets[0]);
	close(sockets[1]);
}

static void expect_key(const char *what, unsigned long long code,
		       int shift, const char *want)
{
	str output;
	s_init(&output);
	cb_key_name(&output, code, shift, 0, 0);
	expect_text(what, output.p, want);
	s_free(&output);
}

int main(void)
{
	static const struct {
		unsigned long long code;
		const char *name;
	} function_keys[] = {
		{ KEY_F1, "f1" }, { KEY_F2, "f2" }, { KEY_F3, "f3" },
		{ KEY_F4, "f4" }, { KEY_F5, "f5" }, { KEY_F6, "f6" },
		{ KEY_F7, "f7" }, { KEY_F8, "f8" }, { KEY_F9, "f9" },
		{ KEY_F10, "f10" }, { KEY_F11, "f11" }, { KEY_F12, "f12" }
	};
	size_t i;

	expect_frame("one-based button cells become zero-based",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_button\",\"button\":0,\"pressed\":true,"
		"\"x\":160.0,\"y\":1736.0,\"column\":21,\"row\":109}}",
		2, "mouse press left 108 20");
	expect_frame("the top-left stream cell remains zero",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_button\",\"button\":0,\"pressed\":false,"
		"\"x\":0.0,\"y\":0.0,\"column\":1,\"row\":1}}",
		2, "mouse release left 0 0");
	expect_frame("normalized middle button keeps its name",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_button\",\"button\":1,\"pressed\":true,"
		"\"x\":160.0,\"y\":1736.0,\"column\":21,\"row\":109}}",
		2, "mouse press middle 108 20");
	expect_frame("normalized right button keeps its name",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_button\",\"button\":2,\"pressed\":true,"
		"\"x\":160.0,\"y\":1736.0,\"column\":21,\"row\":109}}",
		2, "mouse press right 108 20");
	expect_frame("Alt is attached to a button action",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_button\",\"button\":0,\"pressed\":true,"
		"\"x\":160.0,\"y\":1736.0,\"column\":21,\"row\":109,"
		"\"alt\":true}}",
		2, "mouse alt-press left 108 20");
	expect_drag_frame("Alt is retained during a drag",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_motion\",\"x\":160.0,\"y\":1736.0,"
		"\"column\":21,\"row\":109,\"alt\":true}}",
		"mouse alt-drag 108 20");
	expect_frame("Shift is attached to wheel actions",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_wheel\",\"delta\":1,\"x\":160.0,"
		"\"y\":1736.0,\"shift\":true}}",
		2, "mouse shift-wheelup 108 20");
	expect_frame("motion uses the same coordinate contract",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_motion\",\"x\":160.0,\"y\":1736.0,"
		"\"column\":21,\"row\":109}}",
		3, "mouse motion 108 20");
	expect_frame("pixel fallback uses 8 by 16 cell metrics",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_button\",\"button\":0,\"pressed\":true,"
		"\"x\":160.0,\"y\":1736.0}}",
		2, "mouse press left 108 20");
	expect_frame_metrics("pixel fallback follows font scale",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_button\",\"button\":0,\"pressed\":true,"
		"\"x\":320.0,\"y\":3472.0}}",
		2, 16, 32, "mouse press left 108 20");
	expect_frame("wheel up uses the pointer cell",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_wheel\",\"delta\":1,"
		"\"x\":160.0,\"y\":1736.0}}",
		2, "mouse wheelup 108 20");
	expect_frame("wheel down uses the pointer cell",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"pointer_wheel\",\"delta\":-1,"
		"\"x\":160.0,\"y\":1736.0}}",
		2, "mouse wheeldown 108 20");
	expect_frame("resolved keymap text wins over raw code",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"key\",\"code\":21,\"pressed\":true,"
		"\"repeat\":false,\"shift\":false,\"ctrl\":false,"
		"\"alt\":false,\"text\":\"z\"}}",
		0, "z");
	expect_frame("resolved shift is the resulting character",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"key\",\"code\":30,\"pressed\":true,"
		"\"repeat\":false,\"shift\":true,\"ctrl\":false,"
		"\"alt\":false,\"text\":\"A\"}}",
		0, "A");
	expect_frame("resolved shifted punctuation is literal",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"key\",\"code\":53,\"pressed\":true,"
		"\"repeat\":false,\"shift\":true,\"ctrl\":false,"
		"\"alt\":false,\"text\":\"?\"}}",
		0, "?");
	expect_frame("resolved UTF-8 keymap level is preserved",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"key\",\"code\":16,\"pressed\":true,"
		"\"repeat\":false,\"shift\":true,\"ctrl\":false,"
		"\"alt\":false,\"text\":\"\316\251\"}}",
		0, "\316\251");
	expect_frame("resolved space remains a literal key",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"key\",\"code\":57,\"pressed\":true,"
		"\"repeat\":false,\"shift\":false,\"ctrl\":false,"
		"\"alt\":false,\"text\":\" \"}}",
		0, " ");
	expect_frame("resolved JSON escape is decoded",
		"{\"type\":\"input\",\"session_id\":\"session-4\",\"event\":{"
		"\"kind\":\"key\",\"code\":43,\"pressed\":true,"
		"\"repeat\":false,\"shift\":false,\"ctrl\":false,"
		"\"alt\":false,\"text\":\"\\\\\"}}",
		0, "\\");

	for (i = 0; i < sizeof(function_keys) / sizeof(function_keys[0]); ++i) {
		char label[64];
		snprintf(label, sizeof(label), "KEY_%s has Hibr's name",
			 function_keys[i].name);
		expect_key(label, function_keys[i].code, 0, function_keys[i].name);
	}
	expect_key("modifiers prefix a function key", KEY_F10, 1,
		   "shift-f10");
	expect_key("an existing named key stays mapped", KEY_ESC, 0, "escape");

	if (failures != 0) {
		printf("ADAPTER_INPUT_FAIL checks=%d\n", failures);
		return 1;
	}
	printf("ADAPTER_INPUT_PASS pointer_origin=zero buttons=3 wheel=2 "
	       "mouse_modifiers=3 function_keys=12 resolved_text=6\n");
	return 0;
}
