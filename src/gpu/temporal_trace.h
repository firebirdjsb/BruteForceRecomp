#pragma once
#include <stdint.h>

/* Diagnostic protocol only. No temporal resource is declared valid by these
 * records. Each word is emitted as an individual NV097_NO_OPERATION, in one
 * atomically published guest pushbuffer reservation. */
#define BF_MOTION_MARKER 0xBF544D01u
#define BF_MOTION_WORDS 60u
enum { BF_MOTION_VIEW_BEGIN=1, BF_MOTION_VIEW_END=2,
       BF_MOTION_MODEL_BEGIN=3, BF_MOTION_MODEL_END=4,
       BF_MOTION_PART_BEGIN=5, BF_MOTION_PART_END=6 };
typedef struct BFMotionRecord { uint32_t words[BF_MOTION_WORDS]; } BFMotionRecord;
/* Word offsets, not guest-object offsets. The projection is the original 17
 * scalar words at projection+14..57, followed by six original frustum planes. */
enum { BF_MO_VERSION=0,BF_MO_KIND=1,BF_MO_CONTEXT=2,BF_MO_VIEW=3,
       BF_MO_PROJECTION=4,BF_MO_VIEW_FLAGS=5,BF_MO_OBJECT=6,
       BF_MO_GENERATION=7,BF_MO_SLOT=8,BF_MO_RESOURCE=9,BF_MO_NODE=10,
       BF_MO_PART=11,BF_MO_SUBDRAW=12,BF_MO_MATERIAL=13,BF_MO_VIEW_VALID=14,BF_MO_DRAW_TARGET=15,
       BF_MO_PROJECTION_WORDS=16,BF_MO_FRUSTUM=33,BF_MO_SEQUENCE=58,
       BF_MO_CALLSITE=57,BF_MO_CHECKSUM=59 };
/* PART_BEGIN uses word58 for the actual original packet+990 value instead of
 * a sequence number. Event order remains the FIFO order, not this diagnostic. */
#define BF_MO_PACKET_INSTANCE BF_MO_SEQUENCE
/* Unverified retail static-adapter observations only, when DRAW_TARGET=1F5600.
 * These aliases are diagnostics, never a validated span or draw identity. */
#define BF_MO_STATIC_RAW_DATA BF_MO_PART
#define BF_MO_STATIC_RAW_SYNC BF_MO_SUBDRAW
#define BF_MO_STATIC_OBSERVED_GENERATION BF_MO_MATERIAL
#ifdef __cplusplus
extern "C" {
#endif
int bf_motion_trace_configured(void);
/* Executor-thread diagnostic gate after the trigger and before its frame cap. */
int bf_motion_trace_active(void);
/* Persistent semantic observation is separate from bounded diagnostic I/O.
 * Lifetime hooks always observe real ctor/dtor events from startup. */
int bf_motion_observer_enabled(void);
void bf_motion_trace_failure(const char *reason);
void bf_motion_lifetime(uint32_t object,int create);
void bf_motion_scene_lifetime(uint32_t scene,int create);
int bf_motion_record_scene(uint32_t stack,BFMotionRecord *record);
int bf_motion_record_view(uint32_t stack,BFMotionRecord *record);
int bf_motion_record_model(uint32_t object,BFMotionRecord *record);
int bf_motion_record_object(uint32_t object,uint32_t target,uint32_t callsite,BFMotionRecord *record);
int bf_motion_record_part(uint32_t packet,uint32_t descriptor,uint32_t subdraw,BFMotionRecord *record);
int bf_motion_record_end(unsigned kind,BFMotionRecord *record);
/* Returns 1 for protocol words. The executor flushes BOTH draw queues before
 * passing the marker header; subsequent payload words cannot submit geometry. */
int bf_motion_command_word(uint32_t word);
/* Executor-thread snapshot of the current ordered envelope. False outside an
 * observed view or after observer shutdown; VIEW_END does not retain identity.
 * This validates protocol/context/view presence, not temporal/depth readiness.
 * Projection validity remains the explicit BF_MO_VIEW_VALID field. */
int bf_motion_current_record(BFMotionRecord *record);
/* Actor PART scopes retain their proven instance/node/part/subdraw identity.
 * The retail static adapter is unverified and ALWAYS rejected. Its MODEL scope
 * retains raw scene+28/+2C words and observed ctor generation for diagnosis;
 * +2C is a synchronization word, not an established byte length. */
int bf_motion_draw_record(uint32_t original_begin_va,BFMotionRecord *record);
/* Scanner-owned parameter origin. The executor caches it at BEGIN, so END,
 * constants, state changes and queue flushing never replace batch identity. */
extern uint32_t bf_motion_parameter_va;
void bf_motion_trace_ui(uint32_t frame);
void bf_motion_trace_flip(uint32_t frame);
/* Validates a whole typed record before writing any guest bytes. */
int bf_motion_record_valid(const BFMotionRecord *record);
int bf_ui_try_motion_record(uint32_t device,const BFMotionRecord *record);
/* Original 00371E10 arguments are minimum and preferred BYTE windows. Its
 * published watermark is selected_window_end minus the original 0x204 guard. */
int bf_motion_reservation_plan(uint32_t original_bytes,uint32_t *minimum,uint32_t *preferred);
void bf_motion_reservation_log(uint32_t device,uint32_t stack,uint32_t original_bytes,
    uint32_t minimum,uint32_t preferred,int before,int after);
#ifdef __cplusplus
}
#endif
