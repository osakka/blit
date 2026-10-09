#define _GNU_SOURCE

/*
 * Hibr display provider for a Blit-owned PTY session.
 *
 * This module is deliberately a transport adapter.  It keeps only pending
 * mutations and protocol state; the surface, revisions, cell semantics,
 * topology, and presentation state remain authoritative in Blit.
 *
 * Build against the Hibr headers:
 *   tcc -I/path/to/hibr/include -I/path/to/hibr/mods -shared -fPIC \
       -o console-blit.so console-blit.c
 */
#include "hibr.h"
#include "display.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CB_MAX_PENDING 262144
#define CB_HTTP_CAP 262144
#define CB_FLUSH_BODY_LIMIT 6144
#define CB_FLUSH_CELL_LIMIT 256
#define CB_PIXEL_CHUNK 60000
#define CB_IMAGE_UNDER 2U
#define CB_GLYPH_CAP 16
#define CB_MAX_PANES 256

typedef struct {
	int x, y;
	char text[CB_GLYPH_CAP];
	unsigned fg, bg, attr;
} cb_cell;

typedef struct {
	sh *shell;
	int opened;
	int rows, cols;
	int cursor_x, cursor_y, cursor_visible;
	unsigned long long revision;
	unsigned pen_fg, pen_bg, pen_attr;
	char host[128], port[16], surface[128], session[64];
	cb_cell *pending;
	size_t pending_count, pending_capacity;
	int ws_fd;
	int ws_subscribed;
	int mouse_mode;
	unsigned int image_id;
	int image_valid;
	int image_row, image_col, image_height, image_width;
} cb_state;

typedef struct {
	int used;
	char name[128];
	int row, col, height, width;
} cb_pane;

static cb_state cb;
static cb_pane cb_panes[CB_MAX_PANES];

static int cb_trace_enabled(void)
{
	const char *value = getenv("BLIT_ADAPTER_TRACE");
	return value != NULL && (!strcmp(value, "1") || !strcmp(value, "yes"));
}

#define CB_TRACE(...) do { \
	if (cb_trace_enabled()) lg(HIBR_LERR, __VA_ARGS__); \
} while (0)

static long cb_flush(void);
static int cb_prect(const char *name, int *row, int *col, int *h, int *w);

static int cb_write_all(int fd, const void *data, size_t length)
{
	const char *p = (const char *)data;
	size_t done = 0;
	while (done < length) {
		ssize_t n = write(fd, p + done, length - done);
		if (n > 0) { done += (size_t)n; continue; }
		if (n < 0 && errno == EINTR) continue;
		return 0;
	}
	return 1;
}

static int cb_ws_send_frame(unsigned char opcode, const void *payload,
				    size_t length)
{
	unsigned char header[14], mask[4];
	unsigned long seed = (unsigned long)time(NULL) ^
		(unsigned long)getpid() ^ (unsigned long)length;
	unsigned char chunk[4096];
	const unsigned char *bytes = (const unsigned char *)payload;
	size_t header_length = 2, offset = 0, i, chunk_length;
	if (cb.ws_fd < 0 || length > 0x7fffffffffffffffULL) return 0;
	header[0] = (unsigned char)(0x80 | (opcode & 0x0f));
	if (length <= 125) {
		header[1] = (unsigned char)(0x80 | length);
	} else if (length <= 65535) {
		header[1] = 0x80 | 126;
		header[2] = (unsigned char)(length >> 8);
		header[3] = (unsigned char)length;
		header_length = 4;
	} else {
		header[1] = 0x80 | 127;
		for (i = 0; i < 8; ++i)
			header[2 + i] = (unsigned char)(length >> (56 - i * 8));
		header_length = 10;
	}
	mask[0] = (unsigned char)(seed >> 24);
	mask[1] = (unsigned char)(seed >> 16);
	mask[2] = (unsigned char)(seed >> 8);
	mask[3] = (unsigned char)seed;
	memcpy(header + header_length, mask, sizeof(mask));
	header_length += sizeof(mask);
	if (!cb_write_all(cb.ws_fd, header, header_length)) return 0;
	while (offset < length) {
		chunk_length = length - offset;
		if (chunk_length > sizeof(chunk)) chunk_length = sizeof(chunk);
		for (i = 0; i < chunk_length; ++i)
			chunk[i] = bytes[offset + i] ^ mask[(offset + i) % 4];
		if (!cb_write_all(cb.ws_fd, chunk, chunk_length)) return 0;
		offset += chunk_length;
	}
	return 1;
}

static int cb_ws_pixel_hide(void)
{
	char message[256];
	int length;
	if (cb.ws_fd < 0 || !cb.session[0] || !cb.image_valid) return 1;
	length = snprintf(message, sizeof(message),
		"{\"type\":\"pixel_frame_hide\",\"session_id\":\"%s\","
		"\"image_id\":%u}", cb.session, cb.image_id);
	if (length < 0 || (size_t)length >= sizeof(message) ||
		!cb_ws_send_frame(0x1, message, (size_t)length)) return 0;
	cb.image_valid = 0;
	return 1;
}

static int cb_json_number(const char *json, const char *key,
				  unsigned long long *value)
{
	char needle[64];
	const char *p;
	char *end;
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	p = strstr(json, needle);
	if (!p) return 0;
	p += strlen(needle);
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
	if (*p++ != ':') return 0;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
	*value = strtoull(p, &end, 10);
	return end != p;
}

static int cb_json_int(const char *json, const char *key, int *value)
{
	unsigned long long n;
	if (!cb_json_number(json, key, &n)) return 0;
	*value = (int)n;
	return 1;
}

static int cb_http(const char *method, const char *path, const char *body,
			   char *response, size_t capacity)
{
	char request[CB_HTTP_CAP];
	char buffer[CB_HTTP_CAP];
	const char *content;
	size_t used = 0;
	int fd, status;
	int length;
	if (response && capacity) response[0] = '\0';

	fd = hibr_dial(cb.host, cb.port, 0);
	if (fd < 0) {
		lg(HIBR_LERR, "console-blit: dial failed for %s:%s (%s %s)",
			cb.host, cb.port, method, path);
		return 0;
	}
	length = snprintf(request, sizeof(request),
		"%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
		"Content-Type: application/json\r\nContent-Length: %lu\r\n\r\n%s",
		method, path, cb.host, (unsigned long)strlen(body), body);
	if (length < 0 || (size_t)length >= sizeof(request) ||
	    !cb_write_all(fd, request, (size_t)length)) {
		lg(HIBR_LERR, "console-blit: HTTP write failed for %s", path);
		close(fd);
		return 0;
	}
	while (used + 1 < sizeof(buffer)) {
		ssize_t n = read(fd, buffer + used, sizeof(buffer) - used - 1);
		if (n > 0) { used += (size_t)n; continue; }
		if (n < 0 && errno == EINTR) continue;
		break;
	}
	close(fd);
	buffer[used] = '\0';
	if (used == 0) {
		lg(HIBR_LERR, "console-blit: empty HTTP response for %s", path);
		return 0;
	}
	if (sscanf(buffer, "HTTP/%*s %d", &status) != 1 || status < 200 ||
	    status >= 300) {
		/* Preserve a structured revision-conflict body for cb_flush.  Do not
		 * print it to Hibr's stderr: stderr is the same PTY whose terminal
		 * writes can advance the surface revision, creating a conflict/log
		 * feedback loop. Other failures remain diagnostic. */
		content = strstr(buffer, "\r\n\r\n");
		if (content) {
			content += 4;
			if (strlen(content) + 1 <= capacity)
				memcpy(response, content, strlen(content) + 1);
		}
		if (!response[0] || !strstr(response, "revision_conflict"))
			lg(HIBR_LERR, "console-blit: HTTP failure for %s: %.240s", path, buffer);
		return 0;
	}
	content = strstr(buffer, "\r\n\r\n");
	if (!content) {
		lg(HIBR_LERR, "console-blit: HTTP response has no body separator for %s", path);
		return 0;
	}
	content += 4;
	if (strlen(content) + 1 > capacity) {
		lg(HIBR_LERR, "console-blit: HTTP response too large for %s", path);
		return 0;
	}
	memcpy(response, content, strlen(content) + 1);
	return 1;
}

static int cb_surface_path(char *path, size_t capacity)
{
	return snprintf(path, capacity, "/v1/surfaces/%s/cells", cb.surface) > 0;
}

static int cb_add_pending(int x, int y, const char *text)
{
	cb_cell *cell;
	if (x < 0 || y < 0 || x >= cb.cols || y >= cb.rows || !text || !*text)
		return HIBR_FAIL;
	if (cb.pending_count == cb.pending_capacity) {
		size_t next = cb.pending_capacity ? cb.pending_capacity * 2 : 128;
		/* A full-screen Hibr wallpaper is larger than the historical
		 * 16K queue.  If an unusually large surface still reaches the
		 * bound, commit the already encoded cells before accepting the
		 * next one.  Dropping later cells would make the bar/window layer
		 * disappear after the wallpaper had filled the queue. */
		if (next > CB_MAX_PENDING) next = CB_MAX_PENDING;
            if (next <= cb.pending_capacity)
                /* A flush is the application's frame boundary.  Never turn
                 * queue pressure into an implicit commit: that exposes a
                 * clear/redraw half-frame.  The raised bounded capacity is
                 * sufficient for the largest supported logical surface. */
                return HIBR_FAIL;
		if (next <= cb.pending_capacity) return HIBR_FAIL;
		cb.pending = realloc(cb.pending, next * sizeof(*cb.pending));
		if (!cb.pending) { cb.pending_capacity = 0; cb.pending_count = 0; return HIBR_FAIL; }
		cb.pending_capacity = next;
	}
	cell = &cb.pending[cb.pending_count++];
	memset(cell, 0, sizeof(*cell));
	cell->x = x; cell->y = y;
	strncpy(cell->text, text, sizeof(cell->text) - 1);
	cell->fg = cb.pen_fg; cell->bg = cb.pen_bg; cell->attr = cb.pen_attr;
	return HIBR_OK;
}

static void cb_colour_json(char *out, size_t capacity, unsigned colour)
{
	if (colour == DP_DEFAULT)
		snprintf(out, capacity, "null");
	else if ((colour & 0xf000000u) == DP_RGB)
		snprintf(out, capacity, "\"#%06x\"", colour & 0xffffffu);
	else
		snprintf(out, capacity, "null");
}

static size_t cb_style_json(char *out, size_t capacity, const cb_cell *cell)
{
	char fg[32], bg[32];
	cb_colour_json(fg, sizeof(fg), cell->fg);
	cb_colour_json(bg, sizeof(bg), cell->bg);
	return (size_t)snprintf(out, capacity,
		"\"style\":{"
		"\"foreground\":%s,\"background\":%s,"
		"\"bold\":%s,\"dim\":%s,\"underline\":%s,"
		"\"inverse\":%s}", fg, bg,
		cell->attr & DP_BOLD ? "true" : "false",
		cell->attr & DP_DIM ? "true" : "false",
		cell->attr & DP_UNDER ? "true" : "false",
		cell->attr & DP_REV ? "true" : "false");
}

static long cb_flush(void)
{
	char path[256], body[CB_HTTP_CAP], response[4096];
	size_t used;
	int i, sent_count;
	if (!cb.opened) return -1;
	response[0] = '\0';
	if (!cb_surface_path(path, sizeof(path))) return -1;
	while (cb.pending_count > 0) {
		used = (size_t)snprintf(body, sizeof(body),
			"{\"base_revision\":%llu,\"cells\":[",
			cb.revision);
		/* The server's complete HTTP request buffer is 8 KiB, including
		 * headers. Keep the JSON body below that boundary; a fixed cell count
		 * is unsafe because UTF-8 glyphs and styled cells have variable size.
		 * Exceeding the boundary makes the server close the request, which
		 * leaves Hibr retrying an ever-growing frame and presents as severe
		 * blinking/latency. */
		for (i = 0; i < (int)cb.pending_count && i < CB_FLUSH_CELL_LIMIT &&
				used + 768 < CB_FLUSH_BODY_LIMIT; ++i) {
			char escaped[CB_GLYPH_CAP * 2];
			char style[512];
			const char *text = cb.pending[i].text;
			char *q = escaped;
			while (*text && q + 2 < escaped + sizeof(escaped)) {
				if (*text == '"' || *text == '\\') *q++ = '\\';
				*q++ = *text++;
			}
			*q = '\0';
			cb_style_json(style, sizeof(style), &cb.pending[i]);
			used += (size_t)snprintf(body + used, sizeof(body) - used,
				"%s{\"x\":%d,\"y\":%d,\"text\":\"%s\",%s}",
				i ? "," : "", cb.pending[i].x, cb.pending[i].y,
				escaped, style);
		}
		if (i == 0) {
			lg(HIBR_LERR, "console-blit: cannot encode pending cells for %s", path);
			return -1;
		}
		used += (size_t)snprintf(body + used, sizeof(body) - used,
			"],\"presentation\":\"%s\",\"cursor\":{\"x\":%d,\"y\":%d,\"visible\":%s}}",
			i == (int)cb.pending_count ? "commit" : "defer",
			cb.cursor_x, cb.cursor_y, cb.cursor_visible ? "true" : "false");
		CB_TRACE("console-blit: cell request bytes=%lu cells=%d pending=%lu presentation=%s",
			(unsigned long)used, i, (unsigned long)cb.pending_count,
			i == (int)cb.pending_count ? "commit" : "defer");
		if (!cb_http("PUT", path, body, response, sizeof(response))) {
			if (strstr(response, "revision_conflict") &&
			    cb_json_number(response, "revision", &cb.revision))
				continue;
			lg(HIBR_LERR, "console-blit: flush failed surface=%s base_revision=%llu pending=%lu",
				cb.surface, cb.revision, (unsigned long)cb.pending_count);
			return -1;
		}
		if (!cb_json_number(response, "revision", &cb.revision)) {
			lg(HIBR_LERR, "console-blit: flush response has no revision: %.240s", response);
			return -1;
		}
		lg(HIBR_LDBG, "console-blit: flushed %d cells to %s revision=%llu",
			i, cb.surface, cb.revision);
		sent_count = i;
		if (sent_count > 0 && (size_t)sent_count < cb.pending_count)
			memmove(cb.pending, cb.pending + sent_count,
				(cb.pending_count - (size_t)sent_count) * sizeof(*cb.pending));
		cb.pending_count -= (size_t)sent_count;
	}
	return 0;
}

static int cb_open(sh *s)
{
	char path[256], response[CB_HTTP_CAP];
	const char *v;
	if (cb.opened) return HIBR_OK;
	memset(&cb, 0, sizeof(cb));
	cb.ws_fd = -1;
	cb.shell = s;
	v = hibr_get(s, "BLIT_SURFACE_ID");
	if (!v || !*v) v = getenv("BLIT_SURFACE_ID");
	if (!v || !*v) {
		lg(HIBR_LERR, "console-blit: BLIT_SURFACE_ID is missing");
		return HIBR_FAIL;
	}
	snprintf(cb.surface, sizeof(cb.surface), "%s", v);
	v = hibr_get(s, "BLIT_SERVER_PORT");
	if (!v || !*v) v = getenv("BLIT_SERVER_PORT");
	snprintf(cb.port, sizeof(cb.port), "%s", v && *v ? v : "8080");
	v = hibr_get(s, "BLIT_SERVER_HOST");
	if (!v || !*v) v = getenv("BLIT_SERVER_HOST");
	snprintf(cb.host, sizeof(cb.host), "%s", v && *v ? v : "127.0.0.1");
	v = hibr_get(s, "BLIT_SESSION_ID");
	if (!v || !*v) v = getenv("BLIT_SESSION_ID");
	if (v && *v) {
		/* Blit publishes stream targets as session-N, while the child
		 * environment carries only N. Normalize the identity once here. */
		if (!strncmp(v, "session-", 8))
			snprintf(cb.session, sizeof(cb.session), "%s", v);
		else
			snprintf(cb.session, sizeof(cb.session), "session-%s", v);
	}
	snprintf(path, sizeof(path), "/v1/surfaces/%s/cells", cb.surface);
	if (!cb_http("GET", path, "", response, sizeof(response))) {
		lg(HIBR_LERR, "console-blit: cannot open surface %s", cb.surface);
		return HIBR_FAIL;
	}
	if (!cb_json_int(response, "width", &cb.cols) ||
	    !cb_json_int(response, "height", &cb.rows) ||
	    !cb_json_number(response, "revision", &cb.revision)) {
		lg(HIBR_LERR, "console-blit: invalid surface response for %s: %.240s",
			cb.surface, response);
		return HIBR_FAIL;
	}
	/* Hibr's native console starts with its cursor hidden (cn_cvis is a
	 * zero-initialized state).  Match that contract until an explicit
	 * `console cursor row col` request enables it; otherwise the desktop's
	 * own drawing cursor appears as a moving block over wallpaper/savers. */
	cb.cursor_visible = 0;
	cb.opened = 1;
	lg(HIBR_LDBG, "console-blit: opened surface=%s session=%s size=%dx%d revision=%llu",
		cb.surface, cb.session[0] ? cb.session : "(none)",
		cb.cols, cb.rows, cb.revision);
	return HIBR_OK;
}

static void cb_close(sh *s)
{
	(void)s;
	if (cb.ws_fd >= 0) close(cb.ws_fd);
	free(cb.pending);
	memset(&cb, 0, sizeof(cb));
	cb.ws_fd = -1;
}

static int cb_isopen(void) { return cb.opened; }

static void cb_size(int *rows, int *cols)
{
	if (rows) *rows = cb.rows;
	if (cols) *cols = cb.cols;
}

static int cb_resized(void)
{
	int rows, cols;
	char path[256], response[CB_HTTP_CAP];
	if (!cb.opened) return 0;
	snprintf(path, sizeof(path), "/v1/surfaces/%s/cells", cb.surface);
	if (!cb_http("GET", path, "", response, sizeof(response))) return 0;
	if (!cb_json_int(response, "width", &cols) ||
	    !cb_json_int(response, "height", &rows)) return 0;
	if (rows == cb.rows && cols == cb.cols) return 0;
	cb.rows = rows; cb.cols = cols; cb.pending_count = 0;
	return 1;
}

static void cb_pen(unsigned fg, unsigned bg, unsigned attr)
{
	cb.pen_fg = fg; cb.pen_bg = bg; cb.pen_attr = attr;
}

static void cb_clear(void)
{
	int y, x;
	cb_ws_pixel_hide();
	for (y = 0; y < cb.rows; ++y)
		for (x = 0; x < cb.cols; ++x)
			cb_add_pending(x, y, " ");
}

static int cb_put(int row, int col, const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	int x = col;
	while (p && *p && x < cb.cols) {
		char glyph[CB_GLYPH_CAP];
		size_t n = 1;
		if (*p >= 0xc0 && *p < 0xe0) n = 2;
		else if (*p >= 0xe0 && *p < 0xf0) n = 3;
		else if (*p >= 0xf0) n = 4;
		if (n >= sizeof(glyph)) n = 1;
		memcpy(glyph, p, n); glyph[n] = '\0';
		if (cb_add_pending(x, row, glyph) != HIBR_OK) return x - col;
		p += n; ++x;
	}
	return x - col;
}

static void cb_fill(int row, int col, int h, int w, const char *text)
{
	int y, x;
	for (y = row; y < row + h; ++y)
		for (x = col; x < col + w; ++x)
			cb_put(y, x, text);
}

static void cb_cursor(int row, int col, int visible)
{
	cb.cursor_y = row; cb.cursor_x = col; cb.cursor_visible = visible;
}

static int cb_colour(const char *text, unsigned *out)
{
	unsigned value;
	if (!text || !out || text[0] != '#' || strlen(text) != 7) return 0;
	if (sscanf(text + 1, "%06x", &value) != 1) return 0;
	*out = DP_RGB | value;
	return 1;
}

static unsigned cb_attr(const char *text)
{
	unsigned attr = 0;
	if (!text) return 0;
	if (strstr(text, "bold")) attr |= DP_BOLD;
	if (strstr(text, "dim")) attr |= DP_DIM;
	if (strstr(text, "underline")) attr |= DP_UNDER;
	if (strstr(text, "italic")) attr |= DP_ITAL;
	if (strstr(text, "blink")) attr |= DP_BLINK;
	if (strstr(text, "reverse") || strstr(text, "inverse")) attr |= DP_REV;
	if (strstr(text, "strike")) attr |= DP_STRIKE;
	return attr;
}

static int cb_open(sh *s);
static void cb_close(sh *s);
static int cb_colour(const char *text, unsigned *out);
static int cb_resized(void);
static void cb_pen(unsigned fg, unsigned bg, unsigned attr);
static void cb_clear(void);
static int cb_put(int row, int col, const char *text);
static void cb_fill(int row, int col, int h, int w, const char *text);
static void cb_cursor(int row, int col, int visible);
static long cb_flush(void);
static int cb_key(int ms, str *out);
static void cb_mouse(int mode);

static cb_pane *cb_find_pane(const char *name)
{
	int i;
	if (!name || !*name) return 0;
	for (i = 0; i < CB_MAX_PANES; ++i)
		if (cb_panes[i].used && !strcmp(cb_panes[i].name, name))
			return &cb_panes[i];
	return 0;
}

static cb_pane *cb_set_pane(const char *name, int row, int col,
					int height, int width)
{
	cb_pane *p = cb_find_pane(name);
	int i;
	if (!p) {
		for (i = 0; i < CB_MAX_PANES; ++i)
			if (!cb_panes[i].used) { p = &cb_panes[i]; break; }
	}
	if (!p || !name || !*name || height < 1 || width < 1) return 0;
	p->used = 1;
	snprintf(p->name, sizeof(p->name), "%s", name);
	p->row = row; p->col = col; p->height = height; p->width = width;
	return p;
}

static void cb_return(sh *s, const char *value)
{
	hibr_ret(s, value ? value : "");
	if (!s->bind && value) printf("%s\n", value);
}

static int cb_colour_name(const char *text, unsigned *out)
{
	static const struct { const char *name; unsigned value; } colours[] = {
		{ "black", 0x000000 }, { "red", 0xaa0000 },
		{ "green", 0x00aa00 }, { "yellow", 0xaa5500 },
		{ "blue", 0x0000aa }, { "magenta", 0xaa00aa },
		{ "cyan", 0x00aaaa }, { "white", 0xaaaaaa },
		{ "gray", 0x555555 }, { "grey", 0x555555 }
	};
	int i;
	if (!text || !out) return HIBR_FAIL;
	if (!strcmp(text, "default")) { *out = DP_DEFAULT; return HIBR_OK; }
	if (text[0] == '#') return cb_colour(text, out) ? HIBR_OK : HIBR_FAIL;
	for (i = 0; i < (int)(sizeof(colours) / sizeof(colours[0])); ++i)
		if (!strcmp(text, colours[i].name)) {
			*out = DP_RGB | colours[i].value;
			return HIBR_OK;
		}
	return HIBR_FAIL;
}

static int cb_console(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	int rows, cols, row, col, i;
	unsigned fg, bg, attr;
	str result;
	cb_pane *p;
	if (!strcmp(sub, "open")) return cb_open(s);
	if (!strcmp(sub, "close")) { cb_close(s); return HIBR_OK; }
	if (!cb.opened && strcmp(sub, "size") && strcmp(sub, "pane"))
		if (cb_open(s) != HIBR_OK) return HIBR_FAIL;
	if (!strcmp(sub, "size")) {
		cb_size(&rows, &cols); s_init(&result); s_num(&result, rows);
		s_ch(&result, ' '); s_num(&result, cols); cb_return(s, result.p); s_free(&result);
		return HIBR_OK;
	}
	if (!strcmp(sub, "resized") || !strcmp(sub, "reassert"))
		return cb_resized() ? HIBR_OK : HIBR_FAIL;
	if (!strcmp(sub, "clear")) { cb_clear(); return HIBR_OK; }
	if (!strcmp(sub, "pen")) {
		if (ac < 3) { fg = bg = DP_DEFAULT; attr = 0; }
		else if (cb_colour_name(av[2], &fg) != HIBR_OK) return HIBR_FAIL;
		else if (ac > 3 && cb_colour_name(av[3], &bg) != HIBR_OK) return HIBR_FAIL;
		else { bg = ac > 3 ? bg : DP_DEFAULT; attr = 0; for (i = 4; i < ac; ++i) attr |= cb_attr(av[i]); }
		cb_pen(fg, bg, attr); return HIBR_OK;
	}
	if (!strcmp(sub, "cursor")) {
		if (ac > 2 && !strcmp(av[2], "off")) cb_cursor(cb.cursor_y, cb.cursor_x, 0);
		else if (ac >= 4) cb_cursor(atoi(av[2]), atoi(av[3]), 1);
		else return HIBR_FAIL;
		return HIBR_OK;
	}
	if (!strcmp(sub, "gfx")) {
		cb_return(s, "kitty 8 16");
		return HIBR_OK;
	}
	if (!strcmp(sub, "put") || !strcmp(sub, "fill")) {
		int pane_offset = 0;
		if (ac > 2 && !strcmp(av[2], "-p")) {
			if (ac < 4 || !(p = cb_find_pane(av[3]))) return HIBR_FAIL;
			pane_offset = 1;
			row = p->row; col = p->col;
		} else { row = 0; col = 0; }
		if (!strcmp(sub, "put")) {
			int base = pane_offset ? 4 : 2;
			if (ac < base + 3) return HIBR_FAIL;
			return cb_put(row + atoi(av[base]), col + atoi(av[base + 1]), av[base + 2]) >= 0 ? HIBR_OK : HIBR_FAIL;
		}
		{
			int base = pane_offset ? 4 : 2;
			if (ac < base + 5) return HIBR_FAIL;
			cb_fill(row + atoi(av[base]), col + atoi(av[base + 1]),
				atoi(av[base + 2]), atoi(av[base + 3]), av[base + 4]);
			return HIBR_OK;
		}
	}
	if (!strcmp(sub, "flush")) {
		long n = cb_flush(); s_init(&result); s_num(&result, n);
		cb_return(s, result.p); s_free(&result); return n < 0 ? HIBR_FAIL : HIBR_OK;
	}
	if (!strcmp(sub, "key")) {
		int r;
		s_init(&result);
		r = cb_key(ac > 2 ? atoi(av[2]) : -1, &result);
		if (r == 1) { cb_return(s, result.p); s_free(&result); return HIBR_OK; }
		s_free(&result);
		return HIBR_FAIL;
	}
	if (!strcmp(sub, "mouse")) {
		int mode = ac > 2 && !strcmp(av[2], "click") ? 1 :
			ac > 2 && !strcmp(av[2], "drag") ? 2 :
			ac > 2 && !strcmp(av[2], "motion") ? 3 : 0;
		cb_mouse(mode); return HIBR_OK;
	}
	if (!strcmp(sub, "pane")) {
		if (ac > 2 && !strcmp(av[2], "clear")) { memset(cb_panes, 0, sizeof(cb_panes)); return HIBR_OK; }
		if (ac > 2 && !strcmp(av[2], "list")) {
			s_init(&result); for (i = 0; i < CB_MAX_PANES; ++i) if (cb_panes[i].used) { if (result.n) s_ch(&result, ' '); s_cat(&result, cb_panes[i].name); }
			cb_return(s, result.p ? result.p : ""); s_free(&result); return HIBR_OK;
		}
		if (ac == 4 && (!strcmp(av[2], "drop") || !strcmp(av[2], "raise") || !strcmp(av[2], "lower"))) {
			p = cb_find_pane(av[3]); if (!p) return HIBR_FAIL;
			if (!strcmp(av[2], "drop")) memset(p, 0, sizeof(*p));
			return HIBR_OK;
		}
		if (ac == 3) {
			p = cb_find_pane(av[2]); if (!p) return HIBR_FAIL;
			s_init(&result); s_num(&result, p->row); s_ch(&result, ' '); s_num(&result, p->col); s_ch(&result, ' '); s_num(&result, p->height); s_ch(&result, ' '); s_num(&result, p->width); cb_return(s, result.p); s_free(&result); return HIBR_OK;
		}
		if (ac >= 7 && cb_set_pane(av[2], atoi(av[3]), atoi(av[4]), atoi(av[5]), atoi(av[6]))) return HIBR_OK;
		return HIBR_FAIL;
	}
	if (!strcmp(sub, "hit")) {
		if (ac < 4) return HIBR_FAIL;
		for (i = CB_MAX_PANES - 1; i >= 0; --i) if (cb_panes[i].used && atoi(av[2]) >= cb_panes[i].row && atoi(av[2]) < cb_panes[i].row + cb_panes[i].height && atoi(av[3]) >= cb_panes[i].col && atoi(av[3]) < cb_panes[i].col + cb_panes[i].width) { cb_return(s, cb_panes[i].name); return HIBR_OK; }
		cb_return(s, ""); return HIBR_FAIL;
	}
	if (!strcmp(sub, "link") || !strcmp(sub, "darkdefault") || !strcmp(sub, "shade") || !strcmp(sub, "darken") || !strcmp(sub, "reassert")) return HIBR_OK;
	return HIBR_FAIL;
}

/* Input stays on the gateway's default all-subscriptions stream; keeping the
   provider's output path and input mapping in one adapter avoids a second
   terminal semantic model. */
static int cb_ws_read(int fd, void *data, size_t length, int timeout_ms)
{
	char *p = (char *)data;
	size_t used = 0;
	while (used < length) {
		fd_set set;
		struct timeval tv;
		ssize_t n;
		FD_ZERO(&set); FD_SET(fd, &set);
		if (timeout_ms < 0) n = select(fd + 1, &set, 0, 0, 0);
		else { tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
			n = select(fd + 1, &set, 0, 0, &tv); }
		if (n <= 0) return 0;
		n = read(fd, p + used, length - used);
		if (n > 0) { used += (size_t)n; continue; }
		if (n < 0 && errno == EINTR) continue;
		return 0;
	}
	return 1;
}

static int cb_ws_open(void)
{
	char request[512], headers[2048];
	size_t used = 0;
	int fd, n;
	fd = hibr_dial(cb.host, cb.port, 0);
	if (fd < 0) { CB_TRACE("console-blit: WebSocket dial failed %s:%s", cb.host, cb.port); return 0; }
	n = snprintf(request, sizeof(request),
		"GET /v1/stream HTTP/1.1\r\nHost: %s\r\n"
		"Upgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: YmxpdC1jb25zb2xlLXN0cmVhbQ==\r\n"
		"Sec-WebSocket-Version: 13\r\n\r\n", cb.host);
	if (n < 0 || (size_t)n >= sizeof(request) || !cb_write_all(fd, request, (size_t)n))
		{ CB_TRACE("console-blit: WebSocket handshake write failed"); close(fd); return 0; }
	while (used + 1 < sizeof(headers)) {
		ssize_t got = read(fd, headers + used, sizeof(headers) - used - 1);
		if (got > 0) {
			used += (size_t)got;
			headers[used] = '\0';
			if (strstr(headers, "\r\n\r\n")) break;
			continue;
		}
		if (got < 0 && errno == EINTR) continue;
		CB_TRACE("console-blit: WebSocket handshake read failed"); close(fd); return 0;
	}
	if (strncmp(headers, "HTTP/1.1 101", 12) != 0) {
		CB_TRACE("console-blit: WebSocket handshake rejected: %.160s", headers);
		close(fd); return 0;
	}
	cb.ws_fd = fd;
	/* The gateway defaults new streams to all event classes.  This adapter
	 * consumes only input, and its bounded frame reader must never be exposed
	 * to large surface/frame messages.  Client-to-server WebSocket messages
	 * must be masked. */
	{
		char subscription[160];
		unsigned char frame[2 + 4 + sizeof(subscription)];
		unsigned char mask[4];
		unsigned long seed = (unsigned long)time(NULL) ^
			(unsigned long)getpid();
		int subscription_length;
		size_t i;
		subscription_length = snprintf(subscription, sizeof(subscription),
			"{\"subscribe\":[\"input\"],\"owner_session_id\":\"%s\"}",
			cb.session);
		if (subscription_length < 0 ||
		    (size_t)subscription_length >= sizeof(subscription)) {
			close(fd);
			return 0;
		}
		frame[0] = 0x81;
		frame[1] = (unsigned char)(0x80 | subscription_length);
		mask[0] = (unsigned char)(seed >> 24);
		mask[1] = (unsigned char)(seed >> 16);
		mask[2] = (unsigned char)(seed >> 8);
		mask[3] = (unsigned char)seed;
		memcpy(frame + 2, mask, sizeof(mask));
		for (i = 0; i < (size_t)subscription_length; ++i)
			frame[6 + i] = (unsigned char)subscription[i] ^ mask[i % 4];
		if (!cb_write_all(fd, frame, (size_t)subscription_length + 6)) {
			CB_TRACE("console-blit: WebSocket input subscription write failed");
			close(fd);
			cb.ws_fd = -1;
			return 0;
		}
	}
	CB_TRACE("console-blit: WebSocket stream opened session=%s", cb.session);
	return 1;
}

static int cb_ws_text(char *text, size_t capacity, int timeout_ms)
{
	unsigned char header[2], ext[2];
	unsigned long length;
	if (!cb_ws_read(cb.ws_fd, header, 2, timeout_ms)) return 0;
	if ((header[0] & 0x0f) == 8) return 0;
	length = header[1] & 0x7f;
	if (length == 126) {
		if (!cb_ws_read(cb.ws_fd, ext, 2, timeout_ms)) return 0;
		length = ((unsigned long)ext[0] << 8) | ext[1];
	}
	if (length == 127 || length + 1 > capacity) return 0;
	if (!cb_ws_read(cb.ws_fd, text, length, timeout_ms)) return 0;
	text[length] = '\0';
	return (header[0] & 0x0f) == 1;
}

static void cb_key_name(str *out, unsigned long long code,
			int shift, int ctrl, int alt)
{
	const char *name = 0;
	char letter[2];
	if (ctrl) s_cat(out, "ctrl-");
	if (alt) s_cat(out, "alt-");
	if (shift) s_cat(out, "shift-");
	if (code >= KEY_A && code <= KEY_Z) {
		letter[0] = (char)('a' + code - KEY_A); letter[1] = '\0'; s_cat(out, letter); return;
	}
	if (code >= KEY_1 && code <= KEY_9) {
		letter[0] = (char)('1' + code - KEY_1); letter[1] = '\0'; s_cat(out, letter); return;
	}
	if (code == KEY_0) { s_ch(out, '0'); return; }
	switch (code) {
	case KEY_ENTER: name = "enter"; break; case KEY_ESC: name = "escape"; break;
	case KEY_BACKSPACE: name = "backspace"; break; case KEY_TAB: name = "tab"; break;
	case KEY_SPACE: name = "space"; break; case KEY_UP: name = "up"; break;
	case KEY_DOWN: name = "down"; break; case KEY_LEFT: name = "left"; break;
	case KEY_RIGHT: name = "right"; break; case KEY_HOME: name = "home"; break;
	case KEY_END: name = "end"; break; case KEY_PAGEUP: name = "pageup"; break;
	case KEY_PAGEDOWN: name = "pagedown"; break; case KEY_DELETE: name = "delete"; break;
	case KEY_INSERT: name = "insert"; break; default: break;
	}
	if (name) s_cat(out, name);
}

static int cb_key(int ms, str *out)
{
	char frame[1024];
	char session_needle[96];
	unsigned long long code;
	int shift, ctrl, alt;
	if (!cb.opened || !out) return -1;
	if (cb.ws_fd < 0 && !cb_ws_open()) return -1;
	snprintf(session_needle, sizeof(session_needle),
		"\"session_id\":\"%s\"", cb.session);
	if (!cb.ws_subscribed) {
		cb.ws_subscribed = 1;
	}
	for (;;) {
		if (!cb_ws_text(frame, sizeof(frame), ms)) return 0;
		if (strstr(frame, "\"type\":\"input\"") != 0 &&
			(!cb.session[0] || !strstr(frame, session_needle)))
			continue;
		if (strstr(frame, "\"kind\":\"pointer_motion\"") != 0) {
			int x = 0, y = 0, column = 0, row = 0;
			int have_cell = cb_json_int(frame, "column", &column) &&
				cb_json_int(frame, "row", &row);
			if (cb.mouse_mode == 3 && cb_json_int(frame, "x", &x) &&
			    cb_json_int(frame, "y", &y)) {
				if (!have_cell) { column = x / 8 + 1; row = y / 8 + 1; }
				s_cat(out, "mouse motion "); s_num(out, row);
				s_ch(out, ' '); s_num(out, column);
				return 1;
			}
			continue;
		}
		if (strstr(frame, "\"kind\":\"pointer_button\"") != 0) {
			int button = 0, pressed = 0, x = 0, y = 0;
			int column = 0, row = 0;
			const char *name = button == 1 ? "right" : button == 2 ? "middle" : "left";
			if (cb.mouse_mode == 0 || !cb_json_int(frame, "button", &button) ||
			    !cb_json_int(frame, "x", &x) || !cb_json_int(frame, "y", &y))
				continue;
			if (strstr(frame, "\"pressed\":true")) pressed = 1;
			if (!cb_json_int(frame, "column", &column) ||
			    !cb_json_int(frame, "row", &row)) {
				column = x / 8 + 1; row = y / 8 + 1;
			}
			name = button == 1 ? "right" : button == 2 ? "middle" : "left";
			s_cat(out, "mouse "); s_cat(out, pressed ? "press " : "release ");
			s_cat(out, name); s_ch(out, ' '); s_num(out, row);
			s_ch(out, ' '); s_num(out, column);
			return 1;
		}
		if (!strstr(frame, "\"kind\":\"key\"") || !cb_json_number(frame, "code", &code)) continue;
		shift = strstr(frame, "\"shift\":true") != 0;
		ctrl = strstr(frame, "\"ctrl\":true") != 0;
		alt = strstr(frame, "\"alt\":true") != 0;
		cb_key_name(out, code, shift, ctrl, alt);
		return out->n ? 1 : 0;
	}
}

static void cb_mouse(int mode) { cb.mouse_mode = mode; }
static void cb_cellpx(int *w, int *h)
{
	if (w) *w = 8;
	if (h) *h = 16;
}
static int cb_image(sh *s, const char *pane, int row, int col, int h, int w,
			const unsigned char *rgb, int iw, int ih, unsigned flags)
{
	char begin[512];
	int pane_row = 0, pane_col = 0, pane_height = 0, pane_width = 0;
	int x, y, length;
	size_t total, offset, chunk;
	(void)s;
	CB_TRACE("console-blit: image request pane=%s row=%d col=%d cells=%dx%d pixels=%dx%d",
		pane ? pane : "(none)", row, col, h, w, iw, ih);
	if (!cb.opened || !rgb || iw < 1 || ih < 1) {
		CB_TRACE("console-blit: image rejected before transport");
		return 0;
	}
	if (pane != NULL && !cb_prect(pane, &pane_row, &pane_col,
		&pane_height, &pane_width)) {
		CB_TRACE("console-blit: image pane lookup failed: %s", pane);
		return 0;
	}
	if (pane != NULL) { row += pane_row; col += pane_col; }
	if (cb.ws_fd < 0 && !cb_ws_open()) {
		CB_TRACE("console-blit: image transport unavailable");
		return 0;
	}
	if (!cb.image_id) cb.image_id = 1;
	x = col * 8; y = row * 16;
	length = snprintf(begin, sizeof(begin),
		"{\"type\":\"pixel_frame_begin\",\"session_id\":\"%s\","
		"\"image_id\":%u,\"width\":%d,\"height\":%d,"
		"\"format\":24,\"x\":%d,\"y\":%d,\"z\":%d,\"chunked\":true}",
		cb.session, cb.image_id, iw, ih, x, y,
		(flags & CB_IMAGE_UNDER) ? -1 : 0);
	total = (size_t)iw * (size_t)ih * 3U;
	if (length < 0 || (size_t)length >= sizeof(begin) ||
		!cb_ws_send_frame(0x1, begin, (size_t)length)) {
		CB_TRACE("console-blit: pixel_frame_begin write failed");
		cb.image_valid = 0;
		return 0;
	}
	for (offset = 0; offset < total; offset += chunk) {
		chunk = total - offset;
		if (chunk > CB_PIXEL_CHUNK) chunk = CB_PIXEL_CHUNK;
		if (!cb_ws_send_frame(0x2, rgb + offset, chunk)) {
			CB_TRACE("console-blit: pixel frame chunk write failed offset=%lu",
				(unsigned long)offset);
			cb.image_valid = 0;
			return 0;
		}
	}
	if (!cb_ws_send_frame(0x1, "{\"type\":\"pixel_frame_end\"}",
		strlen("{\"type\":\"pixel_frame_end\"}"))) {
		CB_TRACE("console-blit: pixel_frame_end write failed");
		cb.image_valid = 0;
		return 0;
	}
	cb.image_valid = 1;
	cb.image_row = row; cb.image_col = col;
	cb.image_height = h; cb.image_width = w;
	CB_TRACE("console-blit: image frame submitted session=%s image=%u bytes=%lu",
		cb.session, cb.image_id, (unsigned long)total);
	return 1;
}
static int cb_imgkeep(sh *s, int row, int col, int h, int w)
{
	(void)s;
	return cb.image_valid && cb.image_row == row && cb.image_col == col &&
		cb.image_height == h && cb.image_width == w;
}
static int cb_prect(const char *name, int *row, int *col, int *h, int *w)
{
	char path[64], response[CB_HTTP_CAP], needle[256];
	const char *pane;
	if (!cb.opened || !name || !*name || !row || !col || !h || !w)
		return 0;
	snprintf(path, sizeof(path), "/v1/topology");
	if (!cb_http("GET", path, "", response, sizeof(response))) return 0;
	snprintf(needle, sizeof(needle), "\"id\":\"%s\"", name);
	pane = strstr(response, needle);
	if (!pane || !cb_json_int(pane, "x", col) || !cb_json_int(pane, "y", row) ||
	    !cb_json_int(pane, "width", w) || !cb_json_int(pane, "height", h))
		return 0;
	return 1;
}
static void cb_link(const char *uri) { (void)uri; }

static const dp_api cb_api = {
	cb_open, cb_close, cb_isopen, cb_size, cb_resized, cb_pen,
	cb_clear, cb_put, cb_fill, cb_cursor, cb_flush, cb_key,
	cb_colour, cb_attr, cb_mouse, cb_prect, cb_link, cb_image, cb_cellpx,
	cb_imgkeep
};

static int cb_ini(sh *s)
{
	cb.ws_fd = -1;
	return hibr_provide(s, "display", DP_API_VER, (void *)&cb_api);
}

static void cb_fini(sh *s)
{
	cb_close(s);
	hibr_unprovide(s, "display");
}

static const hibr_bi cb_bi[] = {
	{ "console", cb_console, "Blit display and pane operations" },
	HIBR_BI_END
};

HIBR_MODULE_P("console-blit", "0.1", "Blit OpenAPI display provider",
		      cb_bi, cb_ini, cb_fini, "display");
