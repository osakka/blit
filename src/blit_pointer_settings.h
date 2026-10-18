/* Included after the canonical JSON/settings helpers. No storage of its own. */
static int pointer_config_parse(const char *document, BlitPointerConfig *config)
{
    const char *cursor = json_key_cursor(document, "pointer");
    char object[512], key[64], value[128], text[64];
    unsigned int seen = 0, bit;
    int i;
    *config = blit_pointer_default();
    if (cursor == NULL) return 1;
    cursor += strlen("pointer") + 2;
    while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor;
    if (*cursor++ != ':') return 0;
    while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor;
    if (*cursor != '{') return 0;
    if (!json_object_slice(cursor, object, sizeof(object))) return 0;
    cursor = object + 1;
    for (;;) {
        size_t length;
        while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor;
        if (*cursor=='}') break;
        if (*cursor!='"') return 0;
        cursor = json_next_member(cursor,key,sizeof(key),value,sizeof(value));
        if (!cursor) return 0;
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
            if (end == NULL || length == 0 || *end) return 0;
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
    if (runtime->kms) blit_kms_set_pointer_art(runtime->kms, &runtime->pointer_art);
    for (i=0; i<runtime->output_count; ++i) runtime->outputs[i].initialized=0;
    runtime->pointer_dirty_outputs = all_output_mask(runtime);
    runtime->pointer_dirty = runtime->pointer_dirty_outputs != 0;
    runtime->render_pending=1;
}
