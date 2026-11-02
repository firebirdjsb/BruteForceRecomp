/* Included in the private kernel bridge, after its event-shadow functions.
 * A notification is delivered only after its actual GPU command executes. */
int bf_gpu_signal_event(uint32_t guest_va)
{
    int type = -1;
    HANDLE event = ke_guest_event(guest_va, &type);
    if (!event) return 0;
    /* KEVENT SignalState is a 32-bit LONG at byte +4. The title resets this
     * word directly; the existing wait bridge synchronizes the host event. */
    BRIDGE_MEM32(guest_va + 4) = 1;
    return SetEvent(event) != 0;
}
