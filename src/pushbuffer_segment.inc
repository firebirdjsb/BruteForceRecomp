/* Retain unfinished top-level method packets across DMA submissions. A CALL
 * has its own complete stream, so its packet state never overwrites the ring's.
 * Addresses here are the runtime's 64-MB physical contiguous mapping. */
typedef struct BfPbPacket {
    uint32_t left,index,method,subchannel,next;
    int nonincrementing;
} BfPbPacket;
static BfPbPacket bf_pb_pending;

static uint32_t pb_walk_segment(uint32_t va,uint32_t end_va,int in_call,
                                uint32_t *jumps,uint32_t *unknown)
{
    const uint8_t *mem=(const uint8_t *)xbox_GetMemoryOffset();
    const uint32_t limit=0x80000000u+PB_RAM_BYTES;
    BfPbPacket local={0};
    BfPbPacket *packet=in_call?&local:&bf_pb_pending;
    uint32_t words=0;
    if(va<0x80000000u||va>=limit||end_va>limit||(va&3)||(end_va&3)) {
        fprintf(stderr,"[PB] rejected segment %08X..%08X\n",va,end_va);
        return 0;
    }
    if(packet->left&&packet->next!=va) {
        fprintf(stderr,"[PB] interrupted packet: expected %08X, received %08X\n",packet->next,va);
        packet->left=0;
    }
    while(va<end_va&&words<0x100000u) {
        uint32_t w;
        if(packet->left) {
            const uint32_t method=packet->method+(packet->nonincrementing?0:packet->index*4);
            note(packet->subchannel,method);
            if(s_exec_enabled)nv2a_pb_exec_method(packet->subchannel,method,*(const uint32_t *)(mem+va));
            va+=4;++words;++packet->index;--packet->left;packet->next=va;
            continue;
        }
        w=*(const uint32_t *)(mem+va);
        s_hist[s_hist_n%PB_HIST].at=va;s_hist[s_hist_n%PB_HIST].w=w;++s_hist_n;
        va+=4;++words;
        if((w&3u)==1u||(w&0xE0000003u)==0x20000000u) {
            const uint32_t target=((w&3u)==1u?w&0xFFFFFFFCu:w&0x1FFFFFFCu)&0x0FFFFFFFu;
            ++*jumps;
            if(pb_bad_target(w,va-4,target,end_va&0x0FFFFFFFu))break;
            if(!in_call)break; // Ring wrap remains owned by the runtime caller.
            va=0x80000000u|target;
            end_va=limit-va<0x400000u?limit:va+0x400000u;
            continue;
        }
        if((w&0xFFFF0003u)==0x00020000u) {
            if(in_call)break;
            continue;
        }
        if((w&3u)==2u) {
            const uint32_t target=(w&0xFFFFFFFCu)&0x0FFFFFFFu;
            if(pb_bad_target(w,va-4,target,end_va&0x0FFFFFFFu))break;
            if(!in_call) {
                const uint32_t sub=0x80000000u|target;
                const uint32_t sub_end=limit-sub<0x400000u?limit:sub+0x400000u;
                ++s_tot_calls;s_in_call=1;
                words+=pb_walk_segment(sub,sub_end,1,jumps,unknown);
                s_in_call=0;
            }
            continue;
        }
        if((w&0xE0030003u)==0u||(w&0xE0030003u)==0x40000000u) {
            packet->left=(w>>18)&0x7FFu;
            packet->index=0;packet->method=w&0x1FFCu;
            packet->subchannel=(w>>13)&7u;
            packet->nonincrementing=(w&0xE0000000u)==0x40000000u;
            packet->next=va;
            continue;
        }
        ++*unknown;
    }
    if(packet->left&&!in_call&&getenv("BF_TRACE_PUSHBUFFER"))
        fprintf(stderr,"[PB-PENDING] next=%08X method=%04X remaining=%u index=%u\n",
            packet->next,packet->method,packet->left,packet->index);
    return words;
}
