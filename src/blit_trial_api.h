/* Frontend only: reuse canonical settings serialization/JSON readers. The
   privileged helper owns the timer, override and recovery state. */
static int trial_helper_request(const char *command, const char *document,
                                char *reply, size_t capacity, int seconds)
{
    struct sockaddr_un address;
    struct timeval timeout = {seconds, 0};
    int fd, flags, error = 0, ready;
    socklen_t error_size = sizeof(error);
    fd_set writable;
    size_t sent = 0, length = strlen(command), used = 0;
    fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    memset(&address,0,sizeof(address)); address.sun_family = AF_UNIX;
    snprintf(address.sun_path,sizeof(address.sun_path),"%s",getenv("BLIT_UPDATE_SOCKET") ?
        getenv("BLIT_UPDATE_SOCKET") : "/run/blit/update.sock");
    flags = fcntl(fd,F_GETFL,0);
    if (flags < 0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK) < 0) goto failed;
    ready = connect(fd,(struct sockaddr *)&address,sizeof(address));
    if (ready < 0 && errno != EINPROGRESS && errno != EAGAIN) goto failed;
    if (ready < 0) {
        struct timeval connect_timeout = {0,300000};
        FD_ZERO(&writable); FD_SET(fd,&writable);
        if (select(fd+1,NULL,&writable,NULL,&connect_timeout) <= 0 ||
            getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&error_size) < 0 || error) goto failed;
    }
    if (fcntl(fd,F_SETFL,flags) < 0 ||
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) < 0 ||
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)) < 0) goto failed;
    while (sent < length) {
        ssize_t count = send(fd,command+sent,length-sent,MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) goto failed;
        sent += (size_t)count;
    }
    if (document) {
        sent = 0; length = strlen(document);
        while (sent < length) {
            ssize_t count = send(fd,document+sent,length-sent,MSG_NOSIGNAL);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) goto failed;
            sent += (size_t)count;
        }
    }
    while (used+1 < capacity) {
        ssize_t count = read(fd,reply+used,capacity-used-1);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
        if (memchr(reply,'\n',used)) break;
    }
    close(fd); reply[used] = 0;
    return strncmp(reply,"OK",2) == 0;
failed:
    close(fd); snprintf(reply,capacity,"root helper unavailable or request timed out"); return 0;
}
static int trial_snapshot_object(const char *object, const char *omit1, const char *omit2,
                                  const char *omit3, const char *extra, char *out, size_t capacity)
{
    static char value[BLIT_SETTINGS_CAPACITY];
    char key[256], escaped[512];
    const char *cursor = object;
    size_t used = 0;
    int first = 1;
    out[0] = 0;
    if (!json_appendf(out,capacity,&used,"{")) return 0;
    while ((cursor=json_next_member(cursor,key,sizeof(key),value,sizeof(value)))) {
        if ((omit1[0] && !strcmp(key,omit1)) || (omit2[0] && !strcmp(key,omit2)) ||
            (omit3[0] && !strcmp(key,omit3))) continue;
        json_escape(key,escaped,sizeof(escaped));
        if (!json_appendf(out,capacity,&used,"%s\"%s\":%s",first?"":",",escaped,value)) return 0;
        first = 0;
    }
    return json_appendf(out,capacity,&used,"%s%s}",first?"":",",extra);
}
static int trial_snapshot(BlitRuntime *runtime, const char *renderer, char *out, size_t capacity)
{
    static char base[BLIT_SETTINGS_CAPACITY], extra[12288];
    char source[8192], server[8192], login[2048], target[256];
    const char *cursor;
    if (!settings_build(runtime,base,sizeof(base),runtime->settings_revision)) return 0;
    cursor = json_key_cursor(base,"server");
    if (!cursor || !json_object_slice(cursor,source,sizeof(source))) snprintf(source,sizeof(source),"{}");
    snprintf(target,sizeof(target),"\"display_backend\":\"sdl\",\"renderer\":\"%s\",\"expect_desktop\":false",renderer);
    if (!trial_snapshot_object(source,"display_backend","renderer","expect_desktop",target,server,sizeof(server))) return 0;
    cursor = json_key_cursor(base,"login");
    if (!cursor || !json_object_slice(cursor,source,sizeof(source))) snprintf(source,sizeof(source),"{}");
    if (!trial_snapshot_object(source,"enabled","","","\"enabled\":false",login,sizeof(login))) return 0;
    snprintf(extra,sizeof(extra),"\"server\":%s,\"login\":%s",server,login);
    return trial_snapshot_object(base,"server","login","",extra,out,capacity);
}
static const char *trial_space(const char *cursor)
{ while (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t') ++cursor; return cursor; }
static int trial_payload(const char *payload, int restore, char *renderer, int *seconds, char *id)
{
    char key[64], value[128];
    const char *cursor=trial_space(payload), *next;
    unsigned seen=0;
    if (*cursor++ != '{') return 0;
    for (;;) {
        char *end;
        long duration;
        size_t length;
        cursor=trial_space(cursor);
        if (*cursor!='"' || !(next=json_next_member(cursor,key,sizeof(key),value,sizeof(value)))) return 0;
        cursor=next; length=strlen(value);
        while (length && strchr(" \t\r\n",value[length-1])) value[--length]=0;
        if (!restore && !strcmp(key,"renderer") && !(seen&1)) {
            const char *end_string; size_t decoded;
            if (value[0]!='"' || !(end_string=json_unescape(value+1,renderer,16,&decoded)) || *end_string ||
                (strcmp(renderer,"software") && strcmp(renderer,"opengles2"))) return 0;
            seen|=1;
        } else if (!restore && !strcmp(key,"timeout_seconds") && !(seen&2)) {
            if (value[0]<'0' || value[0]>'9' || (value[0]=='0' && value[1])) return 0;
            errno=0; duration=strtol(value,&end,10);
            if (errno || *end || duration<BLIT_TRIAL_MIN_SECONDS || duration>BLIT_TRIAL_MAX_SECONDS) return 0;
            *seconds=(int)duration; seen|=2;
        } else if (restore && !strcmp(key,"id") && !(seen&4)) {
            const char *end_string; size_t decoded;
            if (value[0]!='"' || !(end_string=json_unescape(value+1,id,33,&decoded)) || *end_string || decoded!=32) return 0;
            if (!blit_trial_id_valid(id)) return 0;
            seen|=4;
        } else return 0;
        cursor=trial_space(cursor);
        if (*cursor=='}') return *trial_space(cursor+1)==0 && seen==(restore?4:3);
        if (*cursor++!=',') return 0;
    }
}
static void presentation_trial_request(int fd, BlitRuntime *runtime,
                                       const char *method, const char *request, const char *payload)
{
    char reply[1536], state[1536], command[256], renderer[16]="", id[33]="";
    static char document[BLIT_SETTINGS_CAPACITY];
    int seconds=0, i, restore=!strcmp(method,"DELETE");
    if (!strcmp(method,"GET")) {
        if (!trial_helper_request("TRIAL_STATUS\n",NULL,reply,sizeof(reply),2) ||
            !json_object_slice(reply+3,state,sizeof(state))) goto unavailable;
        send_http(fd,200,"OK","application/json",state); return;
    }
    if (strcmp(method,"POST") && !restore) {
        send_problem(fd,405,"Method Not Allowed","method_not_allowed","Use GET, POST or DELETE."); return;
    }
    if (!trial_payload(payload,restore,renderer,&seconds,id)) {
        send_problem(fd,400,"Bad Request","invalid_presentation_trial",
            "POST requires renderer software/opengles2 and integer timeout_seconds30..120; DELETE requires the32-hex current id. Unknown/duplicate fields are refused."); return;
    }
    if (restore) {
        if (!trial_helper_request("TRIAL_STATUS\n",NULL,reply,sizeof(reply),2) ||
            !json_object_slice(reply+3,state,sizeof(state))) goto unavailable;
        {
            char current[64]; int active=0;
            if (!json_string(state,"id",current,sizeof(current)) || strcmp(current,id) ||
                !json_bool(state,"active",&active) || !active) {
                send_problem(fd,409,"Conflict","stale_trial_id","The trial is no longer active or its id changed."); return;
            }
        }
        snprintf(command,sizeof(command),"TRIAL_RESTORE %s\n",id);
        if (!trial_helper_request(command,NULL,reply,sizeof(reply),2)) goto unavailable;
        send_http(fd,202,"Accepted","application/json",state); return;
    }
    if (blit_trial_id_valid(getenv("BLIT_PRESENTATION_TRIAL_ID")) || !runtime->direct_kms || runtime->output_count < 1 ||
        !runtime->settings_enabled || runtime->settings_dirty || runtime->settings_write_reported) {
        send_problem(fd,409,"Conflict","trial_baseline_not_ready","Requires a clean native-KMS baseline, not another trial."); return;
    }
    for (i=0;i<BLIT_MAX_SESSIONS;++i)
        if (runtime->sessions[i].used && !runtime->sessions[i].child_exited &&
            (runtime->sessions[i].login_identity_state==2 || !runtime->sessions[i].login_manager)) {
            send_problem(fd,409,"Conflict","trial_desktop_in_use","Log out before a disruptive presentation trial."); return;
        }
    if (!if_match_revision(fd,request,runtime->settings_revision)) return;
    if (!trial_snapshot(runtime,renderer,document,sizeof(document))) {
        send_problem(fd,500,"Internal Server Error","trial_snapshot_failed","Snapshot exceeds supported settings bounds."); return;
    }
    snprintf(command,sizeof(command),"TRIAL_PREPARE %d %s %lu\n",seconds,renderer,(unsigned long)strlen(document));
    if (!trial_helper_request(command,document,reply,sizeof(reply),12) ||
        !json_object_slice(reply+3,state,sizeof(state)) || !json_string(state,"id",id,sizeof(id)) || !blit_trial_id_valid(id)) goto unavailable;
    send_http(fd,202,"Accepted","application/json",state);
    snprintf(command,sizeof(command),"TRIAL_ACTIVATE %s\n",id);
    if (!trial_helper_request(command,NULL,reply,sizeof(reply),2))
        diagnostic_log(runtime,"error","trial_activation_failed","Independent recovery remains armed; inspect trial status.");
    return;
unavailable:
    send_problem(fd,503,"Service Unavailable","trial_helper_unavailable",
        "The root helper is unavailable, busy, unsupported or refused the trial; inspect its service and trial state. No unguarded startup is attempted.");
}
