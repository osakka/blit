/* Actual API dispatcher with mocked native baseline and a separately running
   helper fixture. No DRM window, systemd unit or physical frontend is used. */
#define main blit_server_entry_for_trial_api_gate
#include "../../src/blit_server.c"
#undef main
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"TRIAL_API_FAIL line=%d %s\n",__LINE__,#test); return 1; } } while(0)
static int call(BlitRuntime *runtime,const char *method,const char *path,const char *payload,int auth,char *response,size_t capacity)
{
    char request[4096]; int pair[2]; ssize_t count; int status=0;
    if (socketpair(AF_UNIX,SOCK_STREAM,0,pair)<0) return 0;
    snprintf(request,sizeof(request),"%s %s HTTP/1.1\r\n%sContent-Length: %lu\r\n\r\n%s",
        method,path,auth?"Authorization: Bearer gate\r\n":"",(unsigned long)strlen(payload),payload);
    handle_request(pair[0],runtime,NULL,request);
    shutdown(pair[0],SHUT_WR);
    count=read(pair[1],response,capacity-1);
    close(pair[0]); close(pair[1]);
    if (count<=0) return 0;
    response[count]=0; sscanf(response,"HTTP/1.1 %d",&status); return status;
}
int main(int argc,char **argv)
{
    BlitRuntime *r=calloc(1,sizeof(*r));
    char response[16384], reply[1536], id[64], original[BLIT_SETTINGS_CAPACITY], command[128];
    const char *bad[]={"{}","{\"renderer\":\"software\",\"timeout_seconds\":29}",
        "{\"renderer\":\"software\",\"timeout_seconds\":30.5}",
        "{\"renderer\":\"software\",\"timeout_seconds\":030}",
        "{\"renderer\":\"software\",\"timeout_seconds\":30,\"extra\":true}",
        "{\"renderer\":\"software\",\"renderer\":\"software\",\"timeout_seconds\":30}"};
    unsigned i;
    CHECK(argc==2 && r);
    r->direct_kms=1; r->display_backend_kms=1; r->display_backend_configured=1;
    r->output_count=1; r->settings_enabled=1; r->settings_revision=r->settings_saved_revision=7;
    r->font_configured=1; r->font_size_px=22; r->font_scale_tenths=14;
    snprintf(r->font.path,sizeof(r->font.path),"/usr/lib/blit/fonts/BlitModernNerd-Regular.ttf");
    r->pane_columns=1; r->pane_rows=2; r->pane_layout_configured=1;
    r->renderer_configured=1; snprintf(r->renderer_request,sizeof(r->renderer_request),"opengles2");
    r->shutdown_key_enabled=1; r->shutdown_key_code=KEY_BACKSPACE;
    r->shutdown_key_modifiers=(1u<<BLIT_MODIFIER_CTRL)|(1u<<BLIT_MODIFIER_ALT);
    snprintf(r->auth_token,sizeof(r->auth_token),"gate");
    snprintf(r->settings_path,sizeof(r->settings_path),"%s",argv[1]);
    snprintf(r->settings_document,sizeof(r->settings_document),
        "{\"version\":1,\"revision\":7,\"input_devices\":{\"input-persistent\":{\"sensitivity_percent\":123}},"
        "\"font\":{\"font_id\":\"blit-modern\",\"size_px\":22},\"server\":{\"display_backend\":\"kms\",\"renderer\":\"opengles2\",\"pane_layout\":\"1x2\"},"
        "\"login\":{\"enabled\":true,\"preferred_pane_id\":\"pane-display-93-2\",\"fallback\":\"first_available\"}}");
    snprintf(original,sizeof(original),"%s",r->settings_document);
    CHECK(call(r,"GET",BLIT_TRIAL_API_PATH,"",0,response,sizeof(response))==401);
    CHECK(call(r,"GET",BLIT_TRIAL_API_PATH,"",1,response,sizeof(response))==200);
    for (i=0;i<sizeof(bad)/sizeof(*bad);++i)
        CHECK(call(r,"POST",BLIT_TRIAL_API_PATH,bad[i],1,response,sizeof(response))==400);
    r->sessions[0].used=1; r->sessions[0].login_manager=1; r->sessions[0].login_identity_state=2;
    CHECK(call(r,"POST",BLIT_TRIAL_API_PATH,"{\"renderer\":\"software\",\"timeout_seconds\":30}",1,response,sizeof(response))==409);
    r->sessions[0].used=0;
    CHECK(call(r,"POST",BLIT_TRIAL_API_PATH,"{\"renderer\":\"software\",\"timeout_seconds\":30}",1,response,sizeof(response))==202);
    CHECK(json_string(request_body(response),"id",id,sizeof(id)) && blit_trial_id_valid(id));
    CHECK(!strcmp(original,r->settings_document) && r->settings_revision==7 && !r->settings_dirty);
    setenv("BLIT_PRESENTATION_TRIAL_ID",id,1);
    CHECK(call(r,"PUT","/v1/settings","{\"document\":{}}",1,response,sizeof(response))==409);
    CHECK(call(r,"PUT","/v1/fonts","{\"size_px\":23}",1,response,sizeof(response))==409);
    CHECK(call(r,"POST","/v1/control/restart","{}",1,response,sizeof(response))==409);
    CHECK(call(r,"GET","/v1/settings","",1,response,sizeof(response))==200);
    CHECK(strstr(response,"trial-snapshot") && strstr(response,"trial-read-only") && strstr(response,"\"persisted\":false"));
    r->settings_dirty=1; settings_flush(r,1);
    CHECK(access(argv[1],F_OK)<0); /* Canonical write would create this canary. */
    unsetenv("BLIT_PRESENTATION_TRIAL_ID"); r->settings_dirty=0;
    snprintf(command,sizeof(command),"TRIAL_RESTORE %s\n",id);
    CHECK(trial_helper_request(command,NULL,reply,sizeof(reply),2));
    free(r);
    puts("PRESENTATION_TRIAL_API_PASS auth=401 invalid=6 desktop=409 snapshot=canonical_unchanged readonly=3 provenance=explicit persistence=blocked scope=mock_native_and_systemd");
    return 0;
}
