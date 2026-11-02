#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
struct Method {uint32_t channel,number,value;bool operator==(const Method &)const=default;};
static std::vector<uint32_t> memory(64*1024*1024/4);
static std::vector<Method> seen;
static bool software_return;
static bool flip_hold;
// The real decoder runs against an owned RAM image and a collecting test
// backend. Reporting and hardware notification callbacks are unused here.
extern "C" {
void nv2a_pb_scan_segment(uint32_t,uint32_t);
void nv2a_pb_scan(uint32_t);
void nv2a_pb_resync(uint32_t);
ptrdiff_t xbox_GetMemoryOffset(){return ptrdiff_t(reinterpret_cast<uintptr_t>(memory.data())-0x80000000u);}
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t);
void nv2a_pb_exec_method(uint32_t channel,uint32_t number,uint32_t value){
    seen.push_back({channel,number,value});
    // Model the real software handler changing the shared CALL register.
    if(software_return&&number==0x100)*xbox_Nv2aRegPtr(0x324C)=value;
    if(number==0x130&&value==1)flip_hold=true;
}
void nv2a_pb_exec_report(){}
void nv2a_pb_exec_walk_begin(){}
void xbox_Nv2aAckBusyBits(){}
int nv2a_pb_exec_flip_poll(){return flip_hold;}
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t offset){static uint32_t get,subroutine;return offset==0x324C?&subroutine:&get;}
}
int main() {
    // Method payloads deliberately resemble CALL, JUMP and RETURN headers.
    // None may execute as a command when the submission ends mid-packet.
    const std::array<uint32_t,8> stream={
        0x40000000u|(3u<<18)|0x1818u,0x0F150002,0x00001001,0x00020000,
        (3u<<18)|(2u<<13)|0x300u,0x20000000,0x40000000,0x7F800000};
    const std::vector<Method> expected={
        {0,0x1818,0x0F150002},{0,0x1818,0x1001},{0,0x1818,0x20000},
        {2,0x300,0x20000000},{2,0x304,0x40000000},{2,0x308,0x7F800000}};
    for(unsigned split=0;split<=stream.size();++split) {
        const uint32_t base=0x1000+split*0x100;
        std::memcpy(reinterpret_cast<uint8_t *>(memory.data())+base,stream.data(),sizeof stream);
        seen.clear();
        nv2a_pb_scan_segment(0x80000000u+base,0x80000000u+base+split*4);
        nv2a_pb_scan_segment(0x80000000u+base+split*4,0x80000000u+base+sizeof stream);
        if(seen!=expected)return 1;
    }
    // A genuine subroutine completes before the ring's following method.
    memory[0x2000/4]=0x3002;
    memory[0x2004/4]=(1u<<18)|0x304;
    memory[0x2008/4]=1;
    memory[0x3000/4]=(1u<<18)|0x300;
    memory[0x3004/4]=2;
    memory[0x3008/4]=0x20000;
    seen.clear();nv2a_pb_scan_segment(0x80002000,0x8000200C);
    if(seen!=std::vector<Method>{{0,0x300,2},{0,0x304,1}})return 2;
    // Invalid physical calls stop before reading outside the backing store.
    memory[0x4000/4]=0x0F150002;
    seen.clear();nv2a_pb_scan_segment(0x80004000,0x80004004);
    if(!seen.empty())return 3;
    // The DMA walker must follow the ring's actual jump, never infer a wrap
    // from the largest PUT previously observed. Poison the skipped gap with
    // an invalid call to expose any accidental linear scan.
    memory[0x5000/4]=0x6002; // CALL outside the ring, with a real RETURN below.
    memory[0x5004/4]=0x4001; // JUMP backward to the next ring segment.
    memory[0x5008/4]=0x0F150002;
    memory[0x6000/4]=(1u<<18)|0x300;
    memory[0x6004/4]=7;
    memory[0x6008/4]=0x20000;
    memory[0x4000/4]=(2u<<18)|0x304;
    memory[0x4004/4]=0x20000; // Payload, not RETURN.
    memory[0x4008/4]=0x0F150002; // Payload, not invalid CALL.
    *xbox_Nv2aRegPtr(0x800044)=0x5000;
    nv2a_pb_resync(0x5000);seen.clear();
    nv2a_pb_scan(0x4008); // Stop with one method parameter still pending.
    nv2a_pb_scan(0x400C);
    if(seen!=std::vector<Method>{{0,0x300,7},{0,0x304,0x20000},{0,0x308,0x0F150002}})return 4;
    if(*xbox_Nv2aRegPtr(0x800044)!=0x400C)return 5;
    // A reused buffer can return with a patched JUMP after its software
    // handler clears CALL state. The following CALL must then be accepted.
    memory[0x7000/4]=0x8002;
    memory[0x7004/4]=0x9002;
    memory[0x8000/4]=(1u<<18)|0x100;
    memory[0x8004/4]=0x7004;
    memory[0x8008/4]=0x7005;
    memory[0x9000/4]=(1u<<18)|0x304;
    memory[0x9004/4]=42;
    memory[0x9008/4]=0x20000;
    software_return=true;seen.clear();
    *xbox_Nv2aRegPtr(0x800044)=0x7000;
    nv2a_pb_resync(0x7000);nv2a_pb_scan(0x7008);
    if(seen!=std::vector<Method>{{0,0x100,0x7004},{0,0x304,42}})return 6;
    if(*xbox_Nv2aRegPtr(0x324C)!=0x7008)return 7;
    // Pause inside a called method packet and resume its remaining payload
    // before RETURN. GET must name the next unread word, not the submitted PUT.
    memory[0xA000/4]=0xB002;
    memory[0xB000/4]=(2u<<18)|0x130;
    memory[0xB004/4]=1;
    memory[0xB008/4]=9;
    memory[0xB00C/4]=0x20000;
    software_return=false;seen.clear();
    *xbox_Nv2aRegPtr(0x800044)=0xA000;
    nv2a_pb_resync(0xA000);nv2a_pb_scan(0xA004);
    if(seen!=std::vector<Method>{{0,0x130,1}}||*xbox_Nv2aRegPtr(0x800044)!=0xB008)return 8;
    flip_hold=false;nv2a_pb_scan(0xA004);
    if(seen!=std::vector<Method>{{0,0x130,1},{0,0x134,9}}||*xbox_Nv2aRegPtr(0x800044)!=0xA004)return 9;
    std::puts("Actual pushbuffer decoder preserves split method payloads, call order and physical bounds.");
}
