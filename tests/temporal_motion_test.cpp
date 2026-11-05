#include "gpu/temporal_trace_backend.h"
#include "ui/upscaler_state.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>
namespace {
std::vector<uint32_t> memory((8u<<20)/4);
constexpr uint32_t device=0x10000,commands=0x20000,stack=0x30000,view=0x40000,
    projection=0x50000,object=0x60000,packet=0x70000,descriptor=0x80000;
uint32_t &word(uint32_t address){return memory[address/4];}
bool send(const BFMotionRecord &r) {
    word(device)=commands;word(device+4)=commands+1024;
    if(bf_ui_try_motion_record(device,&r)!=1||word(device)!=commands+488)return false;
    for(unsigned i=0;i<61;++i) {
        if(word(commands+i*8)!=0x00040100u||!bf_motion_command_word(word(commands+i*8+4)))return false;
    }
    return true;
}
}
extern "C" {
ptrdiff_t xbox_GetMemoryOffset(){return reinterpret_cast<ptrdiff_t>(memory.data());}
size_t g_xbox_total_ram=8u<<20;
}
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"Motion contract failed line %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(int argc,char **argv) {
    const bool persistent=argc>1&&!std::strcmp(argv[1],"--persistent");
    if(persistent)bf_upscaler_capabilities(1,1,1); // Synthetic supported-mode state; no SDK execution.
    BFMotionRecord v{},m{},p{},end{};
    if(argc>1&&!std::strcmp(argv[1],"--off")) {
        CHECK(!bf_motion_trace_configured());
        CHECK(!bf_motion_record_view(stack,&v)&&!bf_motion_record_model(object,&m));
        bf_motion_lifetime(object,1);bf_motion_trace_flip(0);
        std::puts("PASS default-off motion draw diagnostics do not emit records; lifetime observation remains available.");return 0;
    }
    CHECK(bf_motion_trace_configured());
    word(stack)=0x0006FCE5;word(stack+4)=0x90000;word(stack+8)=view;
    word(view+0x50)=projection;
    float one=1;uint32_t bits;std::memcpy(&bits,&one,4);
    for(unsigned i=0;i<17;++i)word(projection+0x14+i*4)=bits;
    for(unsigned i=0;i<24;++i)word(view+0x58+i*4)=bits;
    // Synthetic equivalent of the exact ctor's uint16 live slot +22, resource
    // +C and model-node +6C. No original executable or mutable process needed.
    word(object+0x20)=42u<<16;word(object+0xC)=0xA0000;word(object+0x6C)=0xB0000;
    bf_motion_lifetime(object,1);
    CHECK(bf_motion_record_view(stack,&v)&&v.words[BF_MO_VIEW_VALID]&&send(v));
    BFMotionRecord current{};CHECK(bf_motion_current_record(&current)&&current.words[BF_MO_VIEW]==view);
    CHECK(bf_motion_record_model(object,&m)&&m.words[BF_MO_GENERATION]==1&&m.words[BF_MO_SLOT]==42&&send(m));
    word(packet+0x990)=object;word(packet+0x80)=0x90000;word(packet+0x8C)=view;
    word(descriptor)=0xB2014;word(descriptor+0x2C)=0xC0000;word(descriptor+0x18)=0xD0000;
    CHECK(bf_motion_record_part(packet,descriptor,0xE0000,&p));
    CHECK(p.words[BF_MO_NODE]==0xB2000&&p.words[BF_MO_PART]==0xC0000&&p.words[BF_MO_SUBDRAW]==0xE0000);
    // Insufficient capacity and corrupt checksum must publish no guest cursor
    // and write no command bytes; a later intact record remains usable.
    word(device)=commands;word(device+4)=commands+484;auto before=memory;
    CHECK(bf_ui_try_motion_record(device,&p)==2&&memory==before);
    auto corrupt=p;corrupt.words[BF_MO_OBJECT]^=4;
    CHECK(bf_ui_try_motion_record(device,&corrupt)==0&&memory==before);
    // A watermark is not the physical end: the original XDK reserves when a
    // mapped cursor reaches/passes it. No command/cursor write is allowed yet.
    word(device)=commands;word(device+4)=commands;before=memory;
    CHECK(bf_ui_try_motion_record(device,&p)==2&&memory==before);
    word(device+4)=commands-4;before=memory;
    CHECK(bf_ui_try_motion_record(device,&p)==2&&memory==before);
    word(device)=0x70000000;word(device+4)=commands;before=memory;
    CHECK(bf_ui_try_motion_record(device,&p)==0&&memory==before);
    uint32_t minimum=0,preferred=0;
    CHECK(bf_motion_reservation_plan(512,&minimum,&preferred)&&minimum==1004&&preferred==1004);
    CHECK(bf_motion_reservation_plan(8192,&minimum,&preferred)&&minimum==4096&&preferred==8192);
    CHECK(bf_motion_reservation_plan(16,&minimum,&preferred)&&minimum==1004&&preferred==1004);
    CHECK(!bf_motion_reservation_plan(0,&minimum,&preferred)&&!bf_motion_reservation_plan(513,&minimum,&preferred));
    // Model both exact original selected windows: preferred, or a remaining
    // tail exactly as large as minimum. After its 0x204 guard, 488 bytes fit.
    for(uint32_t selected:{minimum,preferred}) {
        word(device)=commands;word(device+4)=commands+selected-0x204;
        CHECK(bf_ui_try_motion_record(device,&p)==1&&word(device)==commands+488);
    }
    CHECK(send(p));
    Nv2aSurface surface{};surface.color_va=0x100000;surface.width=640;surface.height=480;
    Nv2aRenderState state{};state.depth_test_enable=1;state.depth_write=1;state.zeta_va=0x200000;
    uint32_t program[1][4]={{1,2,3,4}},ids[3]={32,9,45},indices[3]={0,1,2};float constants[192][4]{},inputs[12]{};
    Nv2aXVsBatch batch{};batch.program=program;batch.ninstr=1;batch.consts=constants;
    batch.vertex_ids=ids;batch.indices=indices;batch.nindices=3;batch.nverts=3;batch.ninputs=1;
    batch.input_mask=1;batch.inputs=inputs;batch.aa[0]=batch.aa[1]=1;
    bf_motion_trace_draw(&surface,&state,2,&batch,1,0);
    bf_motion_trace_draw(&surface,&state,1,nullptr,1,0);
    bf_motion_trace_draw(&surface,&state,0,nullptr,1,0);
    CHECK(bf_motion_record_end(BF_MOTION_PART_END,&end)&&send(end));
    CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
    CHECK(bf_motion_record_end(BF_MOTION_VIEW_END,&end)&&send(end));
    CHECK(!bf_motion_current_record(&current)&&!current.words[BF_MO_CONTEXT]);
    bf_motion_trace_ui(1);bf_motion_trace_draw(&surface,&state,0,nullptr,1,1);bf_motion_trace_flip(1);
    bf_motion_lifetime(object,0);bf_motion_lifetime(object,1);
    // Same native address AND same original live slot must still have a new
    // lifetime generation. The renameable object+4 is intentionally irrelevant.
    word(object+4)=0x12345678;word(stack)=0x0006C64B;
    CHECK(bf_motion_record_view(stack,&v)&&send(v));
    CHECK(bf_motion_record_model(object,&m)&&m.words[BF_MO_GENERATION]==2&&send(m));
    CHECK(bf_motion_record_part(packet,descriptor,0xE0000,&p)&&send(p));
    bf_motion_trace_draw(&surface,&state,2,&batch,2,0);
    CHECK(bf_motion_record_end(BF_MOTION_PART_END,&end)&&send(end));
    // A mismatched packet instance cannot borrow the enclosing model identity.
    word(object)=0xF0000;word(0xF0064)=0x21BCC0;
    CHECK(bf_motion_record_object(object,0x21BCC0,0xE8528,&m)&&m.words[BF_MO_GENERATION]==2&&send(m));
    CHECK(m.words[BF_MO_DRAW_TARGET]==0x21BCC0&&m.words[BF_MO_CALLSITE]==0xE8528);
    word(packet+0x990)=0;
    CHECK(bf_motion_record_part(packet,descriptor,0xE0000,&p)&&p.words[BF_MO_GENERATION]==2&&send(p));
    CHECK(p.words[BF_MO_PACKET_INSTANCE]==0&&p.words[BF_MO_CALLSITE]==0xE8528);
    CHECK(bf_motion_record_end(BF_MOTION_PART_END,&end)&&send(end));
    CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
    constexpr uint32_t child=0x68000;
    word(child+0x20)=44u<<16;word(child+8)=0x90000;word(child+0xC)=0xA4000;bf_motion_lifetime(child,1);
    word(packet+0x990)=child;
    CHECK(bf_motion_record_part(packet,descriptor,0xE0000,&p)&&p.words[BF_MO_OBJECT]==child&&
        p.words[BF_MO_GENERATION]==1&&p.words[BF_MO_SLOT]==44&&send(p));
    CHECK(bf_motion_record_end(BF_MOTION_PART_END,&end)&&send(end));
    word(packet+0x990)=object+4;
    CHECK(bf_motion_record_part(packet,descriptor,0xE0000,&p)&&!p.words[BF_MO_GENERATION]&&send(p));
    bf_motion_trace_draw(&surface,&state,2,&batch,2,0);
    CHECK(bf_motion_record_end(BF_MOTION_PART_END,&end)&&send(end));
    CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
    CHECK(bf_motion_record_end(BF_MOTION_VIEW_END,&end)&&send(end));
    // Unknown objects created before observation remain unproven; never assign
    // a history identity from address, draw order or vertex-buffer hash.
    word(object+0x20)=43u<<16;
    CHECK(bf_motion_record_model(object,&m)&&!m.words[BF_MO_GENERATION]);
    CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end));
    // Preserve real static ctor/resource observations, but never validate a
    // static draw from this unverified retail adapter. Scene+2C is a sync word:
    // choosing an aligned value must not turn it into an accepted byte span.
    constexpr uint32_t scene=0x110000,resource=0x120000,geometry=0x130000,pb=0x80140000;
    word(scene+0x1C)=0x90000;word(scene+0x20)=resource;word(resource+0x10)=geometry;
    word(scene+0x28)=pb;word(scene+0x2C)=512;word(0x90000+0xA40)=scene;
    word(stack+0x100)=0xDA2C5;word(stack+0x104)=scene;word(stack+0x108)=packet;
    CHECK(bf_motion_record_view(stack,&v)&&send(v));bf_motion_scene_lifetime(scene,1);
    CHECK(bf_motion_record_scene(stack+0x100,&m)&&!m.words[BF_MO_GENERATION]&&
        m.words[BF_MO_STATIC_OBSERVED_GENERATION]==1&&m.words[BF_MO_SLOT]==UINT32_MAX&&send(m));
    CHECK(m.words[BF_MO_STATIC_RAW_DATA]==pb&&m.words[BF_MO_STATIC_RAW_SYNC]==512&&m.words[BF_MO_RESOURCE]==resource);
    CHECK(!bf_motion_draw_record(pb+12,&current)&&!current.words[BF_MO_GENERATION]);
    CHECK(!bf_motion_draw_record(pb-4,&current)&&!bf_motion_draw_record(pb+512,&current)&&!bf_motion_draw_record(pb+1,&current));
    CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
    bf_motion_scene_lifetime(scene,0);bf_motion_scene_lifetime(scene,1);
    CHECK(bf_motion_record_scene(stack+0x100,&m)&&!m.words[BF_MO_GENERATION]&&m.words[BF_MO_STATIC_OBSERVED_GENERATION]==2&&send(m));
    CHECK(!bf_motion_draw_record(pb+12,&current));
    CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
    word(0x90000+0xA40)=scene+4;
    CHECK(bf_motion_record_scene(stack+0x100,&m)&&!m.words[BF_MO_GENERATION]&&send(m));
    CHECK(!bf_motion_draw_record(pb+12,&current));CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
    for(uint32_t aligned_sync:{4096u,0x100000u}) {
        word(scene+0x2C)=aligned_sync;
        CHECK(bf_motion_record_scene(stack+0x100,&m)&&!m.words[BF_MO_GENERATION]&&send(m));
        CHECK(m.words[BF_MO_STATIC_RAW_SYNC]==aligned_sync&&!bf_motion_draw_record(pb+12,&current));
        CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
    }
    // Even an otherwise positive actor-shaped PART record cannot bless this
    // known-unverified retail static adapter through another caller.
    word(child)=0xF4000;word(0xF4064)=0x1F5600;word(packet+0x990)=child;
    CHECK(bf_motion_record_object(child,0x1F5600,0xDA2C0,&m)&&m.words[BF_MO_GENERATION]&&send(m));
    CHECK(bf_motion_record_part(packet,descriptor,0xE0000,&p)&&p.words[BF_MO_GENERATION]&&send(p));
    CHECK(!bf_motion_draw_record(pb+12,&current));
    CHECK(bf_motion_record_end(BF_MOTION_PART_END,&end)&&send(end));
    CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
    CHECK(bf_motion_record_end(BF_MOTION_VIEW_END,&end)&&send(end));
    word(stack+8)=0x70000000;
    CHECK(bf_motion_record_view(stack,&v)&&!v.words[BF_MO_VIEW_VALID]);
    CHECK(bf_motion_record_end(BF_MOTION_VIEW_END,&end));
    bf_motion_trace_flip(2);CHECK(!bf_motion_trace_configured());
    CHECK(bool(bf_motion_observer_enabled())==persistent);
    if(persistent) {
        word(stack+8)=view;
        CHECK(bf_motion_record_view(stack,&v)&&send(v)&&bf_motion_current_record(&current));
        CHECK(bf_motion_record_end(BF_MOTION_VIEW_END,&end)&&send(end));
        CHECK(!bf_motion_current_record(&current));
        CHECK(bf_upscaler_select(BF_UPSCALER_FSR1)&&!bf_motion_observer_enabled());
        bf_motion_lifetime(child,0);bf_motion_lifetime(child,1);
        CHECK(bf_upscaler_select(BF_UPSCALER_FSR3)&&bf_motion_observer_enabled());
        CHECK(bf_motion_record_view(stack,&v)&&send(v));
        CHECK(bf_motion_record_model(child,&m)&&m.words[BF_MO_GENERATION]==2&&send(m));
        CHECK(bf_motion_record_end(BF_MOTION_MODEL_END,&end)&&send(end));
        CHECK(bf_motion_record_end(BF_MOTION_VIEW_END,&end)&&send(end));
    }
    std::puts("PASS semantic lifetime reuse, original part/node IDs, ordered packet bounds, all-path coverage and ordinary-view envelope.");
}
