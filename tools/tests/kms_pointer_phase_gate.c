/* Real compositor; only DRM boundary mocked. Independent output deadlines. */
#include "../../src/blit_kms.c"
#include "fixtures/drm_hotplug_mock.c"

#define CHECK(c, why) do { if (!(c)) { fprintf(stderr, "KMS_POINTER_PHASE_FAIL %s\n", why); exit(3); } } while (0)
static unsigned present(BlitKms *kms, float x, int visible, unsigned mask)
{
    uint32_t submitted=0, deferred=0;
    CHECK(blit_kms_present(kms,NULL,0,x,20,visible,mask,NULL,0,1,0,0,
                          &submitted,&deferred)==0,"present failed");
    return submitted;
}
static void complete(BlitKms *kms)
{
    drmEventContext context;
    memset(&context,0,sizeof(context));
    context.version=DRM_EVENT_CONTEXT_VERSION;
    context.page_flip_handler=kms_flip_event;
    CHECK(drmHandleEvent(kms->fd,&context)==0,"complete failed");
}
static unsigned pixel(KmsOutput *output, int x)
{
    return *(uint32_t *)((unsigned char *)output->front.map+20*output->front.pitch+x*4)&0xffffffU;
}
int main(int argc, char **argv)
{
    char state[512], trace[512], device[512]; FILE *file; KmsSnapshot snapshot;
    BlitKms *kms; unsigned mask; int n;
    if(argc!=2)return 2;
    snprintf(state,sizeof(state),"%s/state",argv[1]);
    snprintf(trace,sizeof(trace),"%s/trace",argv[1]);
    snprintf(device,sizeof(device),"%s/device",argv[1]);
    setenv("BLIT_DRM_MOCK_STATE",state,1);setenv("BLIT_DRM_MOCK_TRACE",trace,1);
    file=fopen(state,"w");CHECK(file,"state file");
    fprintf(file,"2 0 0 0\n93 320 192\n94 320 192\n");fclose(file);
    kms=blit_kms_new(NULL,NULL);CHECK(kms,"allocate");
    kms->fd=open(device,O_RDWR|O_CREAT|O_TRUNC,0600);CHECK(kms->fd>=0,"open");
    CHECK(kms_read_snapshot(kms,&snapshot)==0,"snapshot");
    kms_reconcile_snapshot(kms,&snapshot);CHECK(kms->output_count==2,"two outputs");
    blit_kms_set_display_position(kms,0,0,0);
    blit_kms_set_display_position(kms,1,320,0);
    CHECK(present(kms,20,1,3)==3,"initialize");complete(kms);
    /* The idle output gets its deadline first on every motion. */
    for(n=0;n<60;++n) {
        int old=n==0 ? 20 : 40+(n-1)%2*40;
        int x=40+n%2*40;
        CHECK(present(kms,x,1,2)==0,"idle output submitted");
        CHECK(present(kms,x,1,1)==1,"idle output consumed active pointer motion");
        complete(kms);
        CHECK(pixel(kms_output(kms,0),x)==0xffffff,"new pointer missing");
        CHECK(pixel(kms_output(kms,0),old)==0,"old pointer trail");
    }
    /* Crossing: new output first must not suppress old-output erasure. */
    CHECK(present(kms,340,1,2)==2,"cross new output");complete(kms);
    CHECK(present(kms,340,1,1)==1,"cross old output not erased");complete(kms);
    CHECK(pixel(kms_output(kms,0),80)==0,"cross trail");
    CHECK(pixel(kms_output(kms,1),20)==0xffffff,"cross missing");
    CHECK(present(kms,340,0,1)==0,"offscreen hide submitted");
    CHECK(present(kms,340,0,2)==2,"hide consumed by other output");complete(kms);
    CHECK(pixel(kms_output(kms,1),20)==0,"hidden pointer visible");
    /* Deferred flip: latest motion remains outstanding on its own output. */
    CHECK(present(kms,50,1,1)==1,"initial pending flip");
    CHECK(present(kms,70,1,1)==0,"pending flip overwritten");
    mask=present(kms,70,1,2);CHECK(mask==0,"unrelated output after defer");
    complete(kms);
    CHECK(present(kms,70,1,1)==1,"deferred latest motion lost");complete(kms);
    CHECK(pixel(kms_output(kms,0),70)==0xffffff,"deferred pointer missing");
    CHECK(pixel(kms_output(kms,0),50)==0,"deferred trail");
    CHECK(present(kms,340,1,3)==3,"settle second output");complete(kms);
    for(n=0;n<60;++n) {
        int old=n==0 ? 20 : 40+(n-1)%2*40;
        int x=360+n%2*40;
        CHECK(present(kms,x,1,1)==0,"first output idle submitted");
        CHECK(present(kms,x,1,2)==2,"first output consumed second pointer");complete(kms);
        CHECK(pixel(kms_output(kms,1),x-320)==0xffffff,"second pointer missing");
        CHECK(pixel(kms_output(kms,1),old)==0,"second pointer trail");
    }
    CHECK(present(kms,60,1,1)==1,"reverse crossing new output");complete(kms);
    CHECK(present(kms,60,1,2)==2,"reverse crossing erasure");complete(kms);
    setenv("BLIT_KMS_MODESET","1",1);
    CHECK(present(kms,90,1,2)==0,"modeset idle output");
    CHECK(present(kms,90,1,1)==1,"modeset consumed active pointer");
    CHECK(pixel(kms_output(kms,0),90)==0xffffff,"modeset missing pointer");
    blit_kms_close(kms);
    puts("KMS_POINTER_PHASE_PASS idle_first=120 crossings=both_orders hide=per_output pending=latest modeset=per_output pixels=no_trails");
    return 0;
}
