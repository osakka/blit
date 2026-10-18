#ifndef BLIT_CLIENT_NAME_H
#define BLIT_CLIENT_NAME_H

/* ADR0135. No role, runtime ID, timestamp, locale or process counter here. */
static int blit_client_name_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-';
}

static int blit_client_name_make(const char *selector, int ordinal,
                                 char *out, size_t capacity)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i, length;
    int plain = 1, written;
    char prefix[35];
    if (capacity) out[0] = 0;
    if (!selector || !*selector || ordinal < 1 || ordinal > 64) return 0;
    length = strlen(selector);
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)selector[i];
        if (!blit_client_name_char(c)) plain = 0;
        hash = (hash ^ c) * UINT64_C(1099511628211);
        if (i < sizeof(prefix)-1) prefix[i] = blit_client_name_char(c) ? c : '_';
    }
    prefix[length < sizeof(prefix)-1 ? length : sizeof(prefix)-1] = 0;
    /* Reserve enough for the longest ordinal, including the terminator. */
    if (plain && length <= 54)
        written = snprintf(out, capacity, "blit-%s-p%d", selector, ordinal);
    else
        written = snprintf(out, capacity, "blit-%s-%016llx-p%d", prefix,
                           (unsigned long long)hash, ordinal);
    if (written < 1 || written > 63 || (size_t)written >= capacity) {
        if (capacity) out[0] = 0;
        return 0;
    }
    return 1;
}

/* Resolve through canonical inventory order, never through pane-id spelling. */
static int native_client_name_for_target(BlitRuntime *runtime, const char *pane_id,
                                         const char *display_id, char *out,
                                         size_t capacity)
{
    BlitPane *pane = pane_id && *pane_id ? find_pane_id(runtime, pane_id) : NULL;
    int d;
    int i, ordinal = 0, found = pane == NULL;
    char candidate[64];
    if (capacity) out[0] = 0;
    if (pane_id && *pane_id && !pane) return 0;
    if (!pane && (!display_id || !*display_id)) return 0;
    d = display_index_for_id(runtime, pane ? pane->display_id : display_id);
    if (d < 0 || !display_selector(&runtime->displays[d])[0]) return 0;
    for (i = 0; i < runtime->pane_count; ++i) {
        BlitPane *p = &runtime->panes[i];
        if (!p->used || display_index_for_id(runtime, p->display_id) != d) continue;
        ++ordinal;
        if (p == pane) { found = 1; break; }
    }
    if (!found) return 0;
    if (!pane) ordinal = 1;
    if (!blit_client_name_make(display_selector(&runtime->displays[d]), ordinal,
                               candidate, sizeof(candidate))) return 0;
    for (i = 0; i < runtime->display_count; ++i) {
        char other[64];
        if (i != d && blit_client_name_make(display_selector(&runtime->displays[i]),
                                           ordinal, other, sizeof(other)) &&
            !strcmp(candidate, other)) return 0;
    }
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSession *s = &runtime->sessions[i];
        if (s->used && !s->child_exited && !strcmp(s->login_client_name, candidate)) return 0;
    }
    for (i = 0; i < runtime->auxiliary_count; ++i) {
        BlitAuxiliaryBinding *b = &runtime->auxiliary_bindings[i];
        BlitSession *s = find_session_id(runtime, b->session_id);
        if (s && !s->child_exited && !strcmp(b->client_name, candidate)) return 0;
    }
    if (strlen(candidate) >= capacity) return 0;
    snprintf(out, capacity, "%s", candidate);
    return 1;
}
#endif
