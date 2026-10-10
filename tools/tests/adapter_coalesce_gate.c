/*
 * Issue #30. Assert that a Hibr frame which changes nothing sends nothing.
 *
 * Hibr composes every frame by blanking the screen and drawing over it, and
 * it does so whether or not anything moved. The adapter used to forward all
 * of that, so an idle desktop advanced the surface revision roughly seven
 * hundred times a second. The surface revision is the only identity a client
 * has for the surface's content, so churning it makes change undetectable
 * and optimistic concurrency unwinnable.
 *
 * The revision is also the measurement. Blit advances it exactly once per
 * accepted cell request, so the delta across a flush IS the number of
 * requests the adapter sent, which is the quantity under test. Nothing here
 * needs to count HTTP calls separately.
 *
 * This drives the real adapter against a real server: console-blit.c is
 * included rather than linked so the provider's own statics are callable,
 * and the handful of Hibr symbols it needs are stubbed below. There is no
 * Hibr process, which is the point -- the behaviour being gated is the
 * adapter's, and it must be assertable without one.
 */
#define _GNU_SOURCE
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "hibr.h"

/* --- the Hibr runtime, reduced to what the adapter calls ----------------- */

static int cb_gate_verbose;

void lg(int level, const char *format, ...)
{
	va_list args;
	if (!cb_gate_verbose) return;
	va_start(args, format);
	fprintf(stderr, "[hibr lv=%d] ", level);
	vfprintf(stderr, format, args);
	fputc('\n', stderr);
	va_end(args);
}

int hibr_dial(const char *host, const char *port, int udp)
{
	struct addrinfo hints, *list, *entry;
	int fd = -1;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &list) != 0) return -1;
	for (entry = list; entry != NULL; entry = entry->ai_next) {
		fd = socket(entry->ai_family, entry->ai_socktype,
			    entry->ai_protocol);
		if (fd < 0) continue;
		if (connect(fd, entry->ai_addr, entry->ai_addrlen) == 0) break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(list);
	return fd;
}

/* The adapter reads its configuration from the shell first and the
   environment second. Returning NULL here selects the environment, which is
   how this gate configures it. */
const char *hibr_get(sh *s, const char *key) { (void)s; (void)key; return NULL; }
void hibr_ret(sh *s, const char *value) { (void)s; (void)value; }
int hibr_provide(sh *s, const char *name, unsigned ver, void *api)
{
	(void)s; (void)name; (void)ver; (void)api;
	return HIBR_OK;
}
int hibr_unprovide(sh *s, const char *name) { (void)s; (void)name; return HIBR_OK; }

void s_init(str *s) { s->p = NULL; s->n = 0; s->cap = 0; }
void s_free(str *s) { free(s->p); s_init(s); }
void s_add(str *s, const char *p, size_t n)
{
	char *grown = (char *)realloc(s->p, s->n + n + 1);
	if (!grown) return;
	s->p = grown;
	memcpy(s->p + s->n, p, n);
	s->n += n;
	s->p[s->n] = '\0';
	s->cap = s->n + 1;
}
void s_cat(str *s, const char *p) { if (p) s_add(s, p, strlen(p)); }
void s_ch(str *s, int c) { char b = (char)c; s_add(s, &b, 1); }
void s_num(str *s, long v)
{
	char b[32];
	snprintf(b, sizeof(b), "%ld", v);
	s_cat(s, b);
}

#include "console-blit.c"

/* --- the gate ----------------------------------------------------------- */

static int failures;

static void expect(const char *what, unsigned long long got,
		   unsigned long long want)
{
	if (got == want) {
		printf("  ok   %-34s requests=%llu\n", what, got);
		return;
	}
	printf("  FAIL %-34s requests=%llu expected=%llu\n", what, got, want);
	++failures;
}

static void expect_at_least(const char *what, unsigned long long got,
			    unsigned long long floor)
{
	if (got >= floor) {
		printf("  ok   %-34s requests=%llu\n", what, got);
		return;
	}
	printf("  FAIL %-34s requests=%llu expected>=%llu\n", what, got, floor);
	++failures;
}

/* One Hibr frame: blank the screen, then draw. The blanking pass is not an
   artifact of this test; it is what Hibr's console layer does, and it is the
   reason a naive adapter re-sends every cell every frame. */
static void frame(const char *marker)
{
	cb_pen(DP_RGB | 0xffffffu, DP_RGB | 0x000080u, 0);
	cb_clear();
	cb_put(0, 0, "hello");
	cb_put(1, 0, "world");
	cb_put(5, 5, marker);
	cb_flush();
}

/* The adapter's own HTTP helper caps a response at 256 KiB, and a fully
   styled 64x24 snapshot is larger than that, so read it here instead of
   borrowing cb_http. */
#define GATE_SNAPSHOT_CAP (8u * 1024u * 1024u)
static char *gate_snapshot;

static int snapshot_read(void)
{
	char request[512];
	const char *body;
	size_t used = 0;
	int fd;

	if (gate_snapshot == NULL) {
		gate_snapshot = (char *)malloc(GATE_SNAPSHOT_CAP);
		if (gate_snapshot == NULL) return 0;
	}
	fd = hibr_dial(cb.host, cb.port, 0);
	if (fd < 0) return 0;
	snprintf(request, sizeof(request),
		 "GET /v1/surfaces/%s/cells HTTP/1.1\r\nHost: %s\r\n"
		 "Connection: close\r\n\r\n", cb.surface, cb.host);
	if (!cb_write_all(fd, request, strlen(request))) { close(fd); return 0; }
	while (used + 1 < GATE_SNAPSHOT_CAP) {
		ssize_t n = read(fd, gate_snapshot + used,
				 GATE_SNAPSHOT_CAP - used - 1);
		if (n > 0) { used += (size_t)n; continue; }
		if (n < 0 && errno == EINTR) continue;
		break;
	}
	close(fd);
	gate_snapshot[used] = '\0';
	body = strstr(gate_snapshot, "\r\n\r\n");
	if (body == NULL) return 0;
	memmove(gate_snapshot, body + 4, strlen(body + 4) + 1);
	return 1;
}

static int snapshot_has(const char *needle)
{
	if (!snapshot_read()) return -1;
	return strstr(gate_snapshot, needle) != NULL;
}

static long snapshot_count(const char *needle)
{
	const char *p;
	long n = 0;
	if (!snapshot_read()) return -1;
	for (p = gate_snapshot; (p = strstr(p, needle)) != NULL; p += strlen(needle))
		++n;
	return n;
}

/* An open frame withholds the surface from the renderer. A coalesced frame
   is usually one request, so it is sent "immediate" rather than "commit",
   and if "immediate" did not close the frame a surface could be left
   permanently unpresented by a single earlier failure. */
static void expect_frame_closed(const char *what)
{
	char path[256], response[4096];
	snprintf(path, sizeof(path), "/v1/surfaces/%s/summary", cb.surface);
	if (!cb_http("GET", path, "", response, sizeof(response)) ||
	    strstr(response, "\"frame_open\":false") == NULL) {
		printf("  FAIL %-34s frame still open\n", what);
		++failures;
		return;
	}
	printf("  ok   %-34s\n", what);
}

/* Open a frame by hand the way a chunked frame's first request does, then
   prove a coalesced single-request frame closes it again.

   This is the hazard coalescing introduces. Before it, every frame was large
   enough to be chunked and ended in "commit", which closed the frame. After
   it, most frames are one request and are sent "immediate", so if "immediate"
   did not close the frame, one frame abandoned mid-way would leave the
   surface withheld from the renderer forever. The hand-written "defer" below
   stands in for that abandoned frame. */
static void expect_immediate_closes_an_open_frame(void)
{
	char path[256], body[512], response[4096];
	unsigned long long before;

	snprintf(path, sizeof(path), "/v1/surfaces/%s/cells", cb.surface);
	snprintf(body, sizeof(body),
		 "{\"base_revision\":%llu,\"cells\":"
		 "[{\"x\":0,\"y\":10,\"text\":\"-\"}],"
		 "\"presentation\":\"defer\"}", cb.revision);
	if (!cb_http("PUT", path, body, response, sizeof(response)) ||
	    !cb_json_number(response, "revision", &cb.revision)) {
		printf("  FAIL could not open a frame by hand\n");
		++failures;
		return;
	}
	snprintf(path, sizeof(path), "/v1/surfaces/%s/summary", cb.surface);
	if (!cb_http("GET", path, "", response, sizeof(response)) ||
	    strstr(response, "\"frame_open\":true") == NULL) {
		printf("  FAIL a deferred request did not open a frame\n");
		++failures;
		return;
	}
	printf("  ok   %-34s\n", "a deferred request opens a frame");
	/* Deliberately a one-cell frame rather than a full redraw: it must be
	   a single request, because a chunked frame would end in "commit" and
	   close the frame whatever "immediate" does. Exactly one request is
	   the proof that this went out as "immediate". */
	before = cb.revision;
	cb_pen(DP_RGB | 0xffffffu, DP_RGB | 0x000080u, 0);
	cb_put(5, 5, "Y");
	cb_flush();
	expect("one immediate request closes it", cb.revision - before, 1);
	expect_frame_closed("the frame is closed again");
	/* The hand-written request was a foreign write, so the shadow no longer
	   describes cell (0,10); a real one would have produced a revision
	   conflict and forgotten it. Nothing is asserted after this point, but
	   leave the adapter in the state a conflict would have left it. */
	cb_shadow_forget();
}

int main(void)
{
	unsigned long long before;

	cb_gate_verbose = getenv("BLIT_GATE_VERBOSE") != NULL;
	if (cb_open(NULL) != HIBR_OK) {
		fprintf(stderr, "adapter-coalesce gate: cannot open the surface\n");
		return 1;
	}
	printf("adapter-coalesce gate: surface=%s %dx%d revision=%llu\n",
	       cb.surface, cb.cols, cb.rows, cb.revision);

	/* The shadow starts empty, so the first frame is a full send. It is
	   chunked by the request bound, hence many requests rather than one. */
	before = cb.revision;
	frame("Q");
	expect_at_least("first frame sends the screen", cb.revision - before, 2);

	/* The assertion this issue exists for. */
	before = cb.revision;
	frame("Q");
	expect("identical frame sends nothing", cb.revision - before, 0);

	/* ...and it must be silence, not amnesia. */
	if (snapshot_has("\"text\":\"Q\"") != 1) {
		printf("  FAIL identical frame lost content\n");
		++failures;
	} else {
		printf("  ok   identical frame kept its content\n");
	}

	/* One changed cell is one request: a frame costs what it changed. */
	before = cb.revision;
	frame("Z");
	expect("one changed cell costs one request", cb.revision - before, 1);
	if (snapshot_has("\"text\":\"Z\"") != 1 ||
	    snapshot_has("\"text\":\"Q\"") != 0) {
		printf("  FAIL the changed cell did not land\n");
		++failures;
	} else {
		printf("  ok   the changed cell landed\n");
	}

	/* A cursor move with no cell change still has to reach the surface,
	   so it is sent as a cell request with an empty cells array. */
	before = cb.revision;
	cb_cursor(7, 3, 1);
	cb_flush();
	expect("cursor-only move costs one request", cb.revision - before, 1);

	/* A flush with nothing queued and nothing moved is not a request. */
	before = cb.revision;
	cb_flush();
	expect("empty flush sends nothing", cb.revision - before, 0);

	/* Repeated idle frames must not drift: this is the live symptom, where
	   roughly seven hundred requests a second arrived from a still screen. */
	before = cb.revision;
	{
		int i;
		for (i = 0; i < 20; ++i) frame("Z");
	}
	expect("20 idle frames send nothing", cb.revision - before, 0);
	expect_frame_closed("no frame left open");

	/* A frame too large for one request is still chunked, must still end
	   closed, and must still cost only what it changed: forgetting the
	   shadow is the only way to make the whole screen change again. */
	cb_shadow_forget();
	before = cb.revision;
	frame("Z");
	expect_at_least("a chunked frame still works", cb.revision - before, 2);
	expect_frame_closed("a chunked frame ends closed");
	before = cb.revision;
	frame("Z");
	expect("and coalesces again after it", cb.revision - before, 0);

	/* A two-byte glyph is never double width -- Blit's width table has no
	   wide range below U+1100 -- so it must not invalidate its neighbour.
	   A Hibr wallpaper is 32,104 cells of U+00B7, and treating those as
	   possibly wide costs a request per second on a static screen. */
	cb_pen(DP_RGB | 0x16324au, DP_RGB | 0x0d1b2au, 0);
	cb_clear();
	{
		int x;
		for (x = 0; x < 40; ++x) cb_put(3, x, "\xc2\xb7");  /* U+00B7 */
	}
	cb_flush();
	before = cb.revision;
	cb_pen(DP_RGB | 0x16324au, DP_RGB | 0x0d1b2au, 0);
	cb_clear();
	{
		int x;
		for (x = 0; x < 40; ++x) cb_put(3, x, "\xc2\xb7");
	}
	cb_flush();
	expect("a narrow multi-byte glyph is free", cb.revision - before, 0);

	expect_immediate_closes_an_open_frame();

	/* A double-width glyph owns the cell to its right, and Blit clears that
	   cell when a narrow glyph replaces the leader. The adapter does not
	   know widths, so if it claimed to know what the neighbour holds it
	   would skip repainting it and leave a gap beside CJK text. */
	cb_pen(DP_RGB | 0xffffffu, DP_RGB | 0x000080u, 0);
	cb_clear();
	cb_put(8, 2, "\xe4\xb8\xad");          /* U+4E2D, double width */
	cb_flush();
	if (snapshot_has("\"width\":2") != 1) {
		printf("  FAIL a wide glyph did not render wide\n");
		++failures;
	} else {
		printf("  ok   %-34s\n", "a wide glyph renders wide");
	}
	/* Now a plain frame, which queues a blank at both columns. The blank at
	   the leader differs from the shadow and is sent; the blank at the
	   neighbour matches the shadow and would be dropped, except that Blit
	   cleared that cell as a side effect of overwriting the leader. Every
	   cell must therefore end up painted with the pen background -- one
	   unpainted cell beside CJK text is the whole defect. */
	cb_clear();
	cb_flush();
	{
		long painted = snapshot_count("\"background\":\"#000080\"");
		long cells = (long)cb.rows * (long)cb.cols;
		if (snapshot_has("\"width\":2") != 0 || painted != cells) {
			printf("  FAIL %-34s painted=%ld of %ld\n",
			       "replacing it leaves no remnant", painted, cells);
			++failures;
		} else {
			printf("  ok   %-34s painted=%ld\n",
			       "replacing it leaves no remnant", painted);
		}
	}

	before = cb.revision;
	cb_close(NULL);
	if (failures > 0) {
		printf("ADAPTER_COALESCE_FAIL checks=%d\n", failures);
		return 1;
	}
	printf("ADAPTER_COALESCE_PASS revision=%llu\n", before);
	return 0;
}
