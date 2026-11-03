/* Synthetic file, real patched kernel bridge. No title data or guest patches.
 * Layout and thunk dispatch follow the pinned MIT kernel_io_apc fixture. */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*GuestFunction)(void);
extern GuestFunction recomp_lookup_kernel(uint32_t);
extern RECOMP_TLS uint32_t g_eax, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
GuestFunction recomp_lookup(uint32_t va) { (void)va; return NULL; }
GuestFunction recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
/* No GPU commands are expected from this I/O-only fixture. */
uint32_t bf_gpu_semaphore_target(void) { abort(); }
int bf_gpu_software_method(uint32_t value,uint32_t depth) { (void)value;(void)depth;abort(); }
void bruteforce_d3d11_wait(void) { abort(); }

/* All guest fields are 32-bit little-endian slots in a 16-MiB test arena.
 * ANSI_STRING is 2-byte Length, 2-byte MaximumLength, 4-byte Buffer;
 * Xbox OBJECT_ATTRIBUTES is three DWORDs; IOSB is Status + Information. */
enum { THUNKS=0x10000, STACK=0x20000, NAME=0x30000, ANSI=0x30100,
       ATTR=0x30200, IOSB=0x30300, HANDLE_SLOT=0x30400, BUFFER=0x31000,
       OFFSET=0x32000, EVENT_SLOT=0x32100, TIMEOUT=0x32200 };
static unsigned char *memory;
static int failures;
#define WORD(va) (*(uint32_t *)(memory+(va)))
#define CHECK(condition) do { if(!(condition)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition);++failures;} } while(0)
static void call(unsigned slot,const uint32_t *args,unsigned count) {
    unsigned i;WORD(STACK)=0xBEEF0000u|slot;
    for(i=0;i<count;++i)WORD(STACK+4+4*i)=args[i];
    g_esp=STACK;g_eax=0xDEADBEEF;
    { GuestFunction function=recomp_lookup_kernel(WORD(THUNKS+slot*4));
      if(!function){fputs("Missing kernel fixture thunk\n",stderr);exit(1);}function(); }
    CHECK(g_esp==STACK+4+4*count); /* stdcall must pop exactly its own arguments. */
}
static uint32_t open_file(int synchronous) {
    /* Match the title's NtCreateFile path: nine DWORDs, FILE_OPEN=1. */
    uint32_t args[9]={HANDLE_SLOT,0x80100000u,ATTR,IOSB,0,0,1,1,synchronous?0x20u:0};
    call(0,args,9);CHECK(g_eax==0);return g_eax==0?WORD(HANDLE_SLOT):0;
}
static void wait_event(uint32_t event,uint32_t expected) {
    /* NtWaitForSingleObjectEx(handle, UserMode, nonalertable, zero timeout). */
    uint32_t args[4]={event,1,0,TIMEOUT};call(3,args,4);CHECK(g_eax==expected);
}
static void close_handle(uint32_t handle) {uint32_t args[1]={handle};call(4,args,1);CHECK(g_eax==0);}
int main(void) {
    char folder[MAX_PATH],file[MAX_PATH];FILE *stream;uint32_t event=0;
    const char *mode=getenv("RECOMP_ASYNC_IO");const int pending=mode&&*mode&&*mode!='0';
    const uint32_t ordinals[]={190,219,189,234,187,186};unsigned i;
    memory=VirtualAlloc(NULL,16u*1024*1024,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!memory)return 1;
    g_xbox_mem_offset=(ptrdiff_t)memory;
    if(!GetTempPathA(sizeof folder,folder)||!GetTempFileNameA(folder,"bfi",0,file))return 1;
    DeleteFileA(file);if(!CreateDirectoryA(file,NULL))return 1;
    strcpy(folder,file);snprintf(file,sizeof file,"%s\\read.bin",folder);
    stream=fopen(file,"wb");if(!stream)return 1;
    CHECK(fwrite("READ",1,4,stream)==4);fclose(stream);
    for(i=0;i<sizeof ordinals/sizeof ordinals[0];++i)WORD(THUNKS+4*i)=0x80000000u|ordinals[i];
    xbox_kernel_set_thunk_address(THUNKS,sizeof ordinals/sizeof ordinals[0]);
    xbox_kernel_init();xbox_path_init(folder,folder);xbox_kernel_bridge_init();
    {
        const char *path="\\Device\\CdRom0\\read.bin";strcpy((char*)memory+NAME,path);
        *(uint16_t*)(memory+ANSI)=(uint16_t)strlen(path);
        *(uint16_t*)(memory+ANSI+2)=(uint16_t)strlen(path)+1;
        WORD(ANSI+4)=NAME;WORD(ATTR)=0;WORD(ATTR+4)=ANSI;WORD(ATTR+8)=0x40;
    }
    { uint32_t args[4]={EVENT_SLOT,0,0,0};call(2,args,4);CHECK(g_eax==0);event=WORD(EVENT_SLOT); }
    for(i=0;i<2;++i) {
        uint32_t handle=open_file((int)i),args[8]={handle,event,0,0,IOSB,BUFFER,4,OFFSET};
        uint32_t clear[1]={event};call(5,clear,1);CHECK(g_eax==0);
        wait_event(event,0x102u); /* STATUS_TIMEOUT proves completion was not pre-signalled. */
        memset(memory+BUFFER,0,4);WORD(IOSB)=WORD(IOSB+4)=0xDEADBEEF;
        call(1,args,8);
        CHECK(g_eax==((!i&&pending)?0x103u:0)); /* STATUS_PENDING only for async handle + enabled profile. */
        CHECK(WORD(IOSB)==0&&WORD(IOSB+4)==4);
        CHECK(memcmp(memory+BUFFER,"READ",4)==0);
        wait_event(event,0); /* Data and final IOSB are available before the signalled event. */
        close_handle(handle);
    }
    close_handle(event);xbox_kernel_shutdown();
    DeleteFileA(file);RemoveDirectoryA(folder);VirtualFree(memory,0,MEM_RELEASE);
    printf("Asynchronous-read mode %d: %d failure(s)\n",pending,failures);
    return failures?1:0;
}
