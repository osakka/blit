#define _GNU_SOURCE 1
#include <SDL3/SDL.h>
#include <string.h>
static size_t scan_calls, scan_bytes;
static size_t counted_strlen(const char *s)
{ size_t n=strlen(s); ++scan_calls; scan_bytes+=n+1; return n; }
static const unsigned char *expected_source;
static int expected_format, expected_width, expected_height, direct_upload;
static bool checked_upload(SDL_Texture *texture,const SDL_Rect *rect,const void *data,int pitch);
#define strlen counted_strlen
#define SDL_UpdateTexture checked_upload
#define main unused_ingestion_server_main
#include "../../src/blit_server.c"
#undef main
#undef SDL_UpdateTexture
#undef strlen
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"GRAPHICS_INGESTION_FAIL line=%d %s\n",__LINE__,#x);exit(1); } } while(0)
static bool checked_upload(SDL_Texture *texture,const SDL_Rect *rect,const void *data,int pitch)
{
    int i;
    if(expected_source){
        CHECK(!rect&&pitch==expected_width*4);
        direct_upload=data==expected_source;
        for(i=0;i<expected_width*expected_height;++i){
            const unsigned char *p=(const unsigned char*)data+i*4;
            const unsigned char *s=expected_source+i*(expected_format==32?4:3);
            CHECK(!memcmp(p,s,3));CHECK(p[3]==(expected_format==32?s[3]:255));
        }
    }
    return SDL_UpdateTexture(texture,rect,data,pitch);
}
static void decode_tests(int observe)
{
    unsigned char buffer[16384];
    char payload[16385];
    int sizes[]={64,512,4096,8192,16380},i,j;
    for(i=0;i<5;++i){
        BlitGraphicsImage image={0};
        size_t n=sizes[i];
        for(j=0;j<(int)n;j+=4)memcpy(payload+j,"QUJD",4);
        payload[n]=0;memset(buffer,0xa5,sizeof(buffer));
        image.pixels=buffer;image.capacity=n/4*3;
        scan_calls=scan_bytes=0;
        CHECK(kitty_decode_chunk(&image,payload));
        CHECK(image.bytes==image.capacity&&!image.base64_tail_length);
        for(j=0;j<(int)image.bytes;++j)CHECK(buffer[j]=="ABC"[j%3]);
        CHECK(buffer[image.capacity]==0xa5);
        printf("DECODE_WORK payload=%lu strlen_calls=%lu length_scan_bytes=%lu\n",
            (unsigned long)n,(unsigned long)scan_calls,(unsigned long)scan_bytes);
        if(!observe)CHECK(scan_calls<=1&&scan_bytes<=n+1);
    }
    for(i=0;i<=8;++i){
        const unsigned char want[]={0,1,2,255,254,253};
        const char *all="AAEC//79";
        BlitGraphicsImage image={0};
        memcpy(payload,all,i);payload[i]=0;
        image.pixels=buffer;image.capacity=6;
        CHECK(kitty_decode_chunk(&image,payload));
        CHECK(kitty_decode_chunk(&image,all+i));
        CHECK(image.bytes==6&&!image.base64_tail_length&&!memcmp(buffer,want,6));
    }
    { const char *encoded[]={"TQ==","TWE=","AA==","AAA="};
      const unsigned char want[][2]={{'M',0},{'M','a'},{0,0},{0,0}};
      for(i=0;i<4;++i)for(j=0;j<=4;++j){
          BlitGraphicsImage image={0};size_t bytes=i%2?2:1;
          memcpy(payload,encoded[i],j);payload[j]=0;memset(buffer,0xa5,sizeof(buffer));
          image.pixels=buffer;image.capacity=bytes;
          CHECK(kitty_decode_chunk(&image,payload));CHECK(kitty_decode_chunk(&image,encoded[i]+j));
          CHECK(image.bytes==bytes&&!image.base64_tail_length&&!memcmp(buffer,want[i],bytes));
          CHECK(buffer[bytes]==0xa5);
      } }
    { BlitGraphicsImage image={0};image.pixels=buffer;image.capacity=3;
      CHECK(kitty_decode_chunk(&image,"Q U\tJ\rD\n"));CHECK(image.bytes==3&&!memcmp(buffer,"ABC",3)); }
    for(i=0;i<3;++i){
        BlitGraphicsImage image={0};image.pixels=buffer;image.capacity=i;
        memset(buffer,0xa5,sizeof(buffer));CHECK(!kitty_decode_chunk(&image,"QUJD"));
        CHECK(buffer[i]==0xa5);
    }
    {const char *bad[]={"@@@@","=AAA","A===","AA?A"};
      for(i=0;i<4;++i){BlitGraphicsImage image={0};image.pixels=buffer;image.capacity=8;CHECK(!kitty_decode_chunk(&image,bad[i]));}}
}
static void upload_tests(int observe)
{
    BlitOutput output={0};BlitGraphicsImage image={0};
    unsigned char rgba[24]={255,0,0,128, 0,255,0,255, 0,0,255,0, 20,30,40,64, 50,60,70,192, 80,90,100,255};
    unsigned char rgb[18]={255,0,0, 0,255,0, 0,0,255, 20,30,40, 50,60,70, 80,90,100};
    const char *name=getenv("BLIT_TEST_RENDERER");SDL_Window *window;
    CHECK(SDL_Init(SDL_INIT_VIDEO));if(!name)name="software";
    window=SDL_CreateWindow("ingestion",8,8,SDL_WINDOW_HIDDEN);CHECK(window);
    output.renderer=SDL_CreateRenderer(window,name);CHECK(output.renderer);
    CHECK(!strcmp(SDL_GetRendererName(output.renderer),name));
    image.width=3;image.height=2;image.format=32;image.pixels=rgba;image.generation=1;
    expected_source=rgba;expected_format=32;expected_width=3;expected_height=2;
    CHECK(sdl_graphics_texture(&output,&image));
    printf("UPLOAD_WORK renderer=%s format=32 direct=%d\n",name,direct_upload);
    if(!observe)CHECK(direct_upload);
    CHECK(output.graphics_texture_uploads==1);
    CHECK(sdl_graphics_texture(&output,&image));CHECK(output.graphics_texture_uploads==1);
    rgba[0]=210;++image.generation;CHECK(sdl_graphics_texture(&output,&image));CHECK(output.graphics_texture_uploads==2);
    if(!observe)CHECK(direct_upload);
    image.format=24;image.pixels=rgb;++image.generation;
    expected_source=rgb;expected_format=24;
    CHECK(sdl_graphics_texture(&output,&image));CHECK(!direct_upload);
    expected_source=NULL;destroy_sdl_graphics_cache(&output);
    SDL_DestroyRenderer(output.renderer);SDL_DestroyWindow(window);SDL_Quit();
}
int main(int argc,char **argv)
{
    int observe=argc==2&&!strcmp(argv[1],"--observe");
    decode_tests(observe);upload_tests(observe);
    puts(observe ? "GRAPHICS_INGESTION_OBSERVATION performance_assertions=disabled" :
        "GRAPHICS_INGESTION_PASS fragmented=binary padding=exact whitespace=accepted bounds=guarded malformed=rejected rgba_rgb=exact");
    return 0;
}
