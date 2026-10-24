/* Included after the canonical JSON/settings helpers. No storage of its own. */
static int pointer_member_key_decode(char *key, size_t capacity)
{
    char quoted[260];
    const char *end;
    size_t length;
    int n = snprintf(quoted,sizeof(quoted),"\"%s\"",key);
    if (n < 0 || (size_t)n >= sizeof(quoted)) return 0;
    end = json_unescape(quoted+1,key,capacity,&length);
    return end && !*end && length > 0 && strlen(key)==length;
}

static int pointer_config_parse(const char *document, BlitPointerConfig *config)
{
    const char *cursor = document, *next;
    static char member[BLIT_SETTINGS_CAPACITY];
    char member_key[128];
    char object[512], key[64], value[128], text[64];
    unsigned int seen = 0, bit;
    int i, present = 0;
    *config = blit_pointer_default();
    while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor;
    if (*cursor++ != '{') return 0;
    while ((next = json_next_member(cursor,member_key,sizeof(member_key),member,sizeof(member)))) {
        cursor = next;
        if (!pointer_member_key_decode(member_key,sizeof(member_key))) return 0;
        if (strcmp(member_key,"pointer")) continue;
        if (present++ || member[0]!='{' || strlen(member)>=sizeof(object)) return 0;
        memcpy(object,member,strlen(member)+1);
    }
    while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor;
    if (*cursor!='}') return 0;
    if (!present) return 1;
    cursor = object + 1;
    for (;;) {
        size_t length;
        while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor;
        if (*cursor=='}') break;
        if (*cursor!='"') return 0;
        cursor = json_next_member(cursor,key,sizeof(key),value,sizeof(value));
        if (!cursor) return 0;
        if (!pointer_member_key_decode(key,sizeof(key))) return 0;
        length=strlen(value);
        while (length && (value[length-1]==' ' || value[length-1]=='\n' ||
               value[length-1]=='\r' || value[length-1]=='\t')) value[--length]='\0';
        if (!strcmp(key, "shape") || !strcmp(key, "palette")) {
            const char *const *names = !strcmp(key,"shape") ? blit_pointer_shapes : blit_pointer_palettes;
            int count = !strcmp(key,"shape") ? 3 : 4;
            size_t length = 0;
            const char *end;
            bit = count == 3 ? 1 : 2;
            if (value[0] != '"') return 0;
            end = json_unescape(value+1, text, sizeof(text), &length);
            if (end == NULL || length == 0 || *end || strlen(text)!=length) return 0;
            for (i = 0; i < count && strcmp(text,names[i]); ++i) {}
            if (i == count) return 0;
            if (count == 3) config->shape = i; else config->palette = i;
        } else if (!strcmp(key,"size_px")) {
            char *end;
            long number;
            bit = 4;
            for (i=0; value[i]; ++i) if (value[i] < '0' || value[i] > '9') return 0;
            if (i == 0 || (i > 1 && value[0] == '0')) return 0;
            number = strtol(value,&end,10);
            if (*end || number < 24 || number > 64) return 0;
            config->size = (int)number;
        } else if (!strcmp(key,"shadow")) {
            bit = 8;
            if (!strcmp(value,"true")) config->shadow = 1;
            else if (!strcmp(value,"false")) config->shadow = 0;
            else return 0;
        } else return 0;
        if (seen & bit) return 0;
        seen |= bit;
        while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor;
        if (*cursor==',') {
            ++cursor;
            while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor;
            if (*cursor=='}') return 0;
        } else if (*cursor!='}') return 0;
    }
    return 1;
}

static void settings_apply_pointer(BlitRuntime *runtime)
{
    BlitPointerConfig config;
    int i;
    if (!pointer_config_parse(runtime->settings_document, &config)) {
        config = blit_pointer_default();
        diagnostic_log(runtime,"warning","invalid_settings_pointer",
                       "Stored pointer appearance is invalid; using the default without rewriting it.");
    }
    if (runtime->pointer_art.revision &&
        !memcmp(&config,&runtime->pointer_art.config,sizeof(config))) return;
    blit_pointer_build(&runtime->pointer_art, config);
    /* update_pointer_shape derives the drawn art from this and hands it to
       KMS; the API art itself is never drawn directly. */
    for (i=0; i<runtime->output_count; ++i) runtime->outputs[i].initialized=0;
    runtime->pointer_dirty_outputs = all_output_mask(runtime);
    runtime->pointer_dirty = runtime->pointer_dirty_outputs != 0;
    runtime->render_pending=1;
}
