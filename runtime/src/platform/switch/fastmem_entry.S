/*
 * Fault-driven guest MMIO loads (Switch).
 *
 * Strong override of libnx's weak __libnx_exception_entry. On a user exception
 * the kernel re-enters crt0 with x0 = error_desc and x1 = ThreadExceptionFrameA64 *
 * (in this process's local region). The frame carries only x0-x8, lr, sp, pc,
 * pstate, afsr0/1, esr and far; x9-x30 and q0-q31 are still live in the register
 * file at this point (libnx's own stub relies on that too), so the whole context
 * is saved here before any C code runs.
 *
 * The C handler returns the pc to resume at, or 0 to decline. A declined fault
 * prints one line and exits: resuming past a load that was not emulated would
 * continue with a stale destination register.
 *
 * The kernel restores x0-x8, lr, sp, pc and pstate from the frame, and takes
 * x9-x29/q0-q31 from the live state at the svc, which is why the slot restore
 * below is ordered the way it is. Frame and slot offsets are asserted against
 * libnx's header in fastmem_fault.cpp.
 *
 * Resume is svcReturnFromException(0) = svc #0x28, the same call libnx's abort
 * path uses.
 */

    .equ    FRAME_LR,       0x48
    .equ    FRAME_SP,       0x50
    .equ    FRAME_PC,       0x58
    .equ    FRAME_PSTATE,   0x60
    .equ    FRAME_ESR,      0x6C
    .equ    FRAME_FAR,      0x70

    .equ    SLOT_X,         0x200
    .equ    SLOT_X_LR,      0x2F0
    .equ    SLOT_SP,        0x2F8
    .equ    SLOT_PC,        0x300
    .equ    SLOT_PSTATE,    0x308
    .equ    SLOT_FRAME,     0x310
    .equ    SLOT_ESR,       0x318
    .equ    SLOT_FAR,       0x320
    .equ    SLOT_SIZE,      0x4000
    .equ    SLOT_COUNT,     8
    .equ    SLOT_SHIFT,     14
    .equ    SLOT_MASK,      0xFF

    .section .text.__libnx_exception_entry, "ax", %progbits
    .global __libnx_exception_entry
    .type   __libnx_exception_entry, %function
    .align  2
__libnx_exception_entry:
    /* No usable frame: nothing can be resumed safely. */
    cbz     x1, .Lno_frame

    /* Park x9-x30 on the kernel-provided exception stack while a slot is
       claimed. Their faulting values are moved into the slot below. */
    stp     x9,  x10, [sp, #-16]!
    stp     x11, x12, [sp, #-16]!
    stp     x13, x14, [sp, #-16]!
    stp     x15, x16, [sp, #-16]!
    stp     x17, x18, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x25, x26, [sp, #-16]!
    stp     x27, x28, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!

    /* Claim the lowest free slot with an ldaxr/stxr bitmask. Slots are only
       held for the length of one emulated access, so the bounded retry is a
       guard against a leaked bit, not an expected wait. */
    adrp    x9, g_fastmemSlotMask
    add     x9, x9, :lo12:g_fastmemSlotMask
    mov     w10, #1
    mov     w16, #1024
.Lclaim:
    ldaxr   w11, [x9]
    mvn     w12, w11
    and     w12, w12, #SLOT_MASK
    cbz     w12, .Lclaim_retry
    rbit    w13, w12
    clz     w13, w13
    lsl     w12, w10, w13
    orr     w14, w11, w12
    stxr    w15, w14, [x9]
    cbnz    w15, .Lclaim
    b       .Lclaimed

.Lclaim_retry:
    subs    w16, w16, #1
    b.eq    .Lslots_busy
    yield
    b       .Lclaim

.Lclaimed:
    /* x13 = slot index. x19 = &slot->x[0], x20 = slot stack top,
       x21 = frame, x23 = slot base for the vector saves. */
    adrp    x23, g_fastmemSlots
    add     x23, x23, :lo12:g_fastmemSlots
    lsl     x9, x13, #SLOT_SHIFT
    add     x23, x23, x9
    add     x19, x23, #SLOT_X
    add     x20, x23, #4, lsl #12
    mov     x21, x1

    /* q0-q31. */
    stp     q0,  q1,  [x23, #0x000]
    stp     q2,  q3,  [x23, #0x020]
    stp     q4,  q5,  [x23, #0x040]
    stp     q6,  q7,  [x23, #0x060]
    stp     q8,  q9,  [x23, #0x080]
    stp     q10, q11, [x23, #0x0A0]
    stp     q12, q13, [x23, #0x0C0]
    stp     q14, q15, [x23, #0x0E0]
    stp     q16, q17, [x23, #0x100]
    stp     q18, q19, [x23, #0x120]
    stp     q20, q21, [x23, #0x140]
    stp     q22, q23, [x23, #0x160]
    stp     q24, q25, [x23, #0x180]
    stp     q26, q27, [x23, #0x1A0]
    stp     q28, q29, [x23, #0x1C0]
    stp     q30, q31, [x23, #0x1E0]

    /* x0-x8 from the frame. */
    ldp     x9,  x10, [x21, #0x00]
    stp     x9,  x10, [x19, #0x00]
    ldp     x11, x12, [x21, #0x10]
    stp     x11, x12, [x19, #0x10]
    ldp     x13, x14, [x21, #0x20]
    stp     x13, x14, [x19, #0x20]
    ldp     x15, x16, [x21, #0x30]
    stp     x15, x16, [x19, #0x30]
    ldr     x17,      [x21, #0x40]
    str     x17,      [x19, #0x40]

    /* lr, sp, pc, pstate, the frame pointer, esr and far. */
    ldr     x9, [x21, #FRAME_LR]
    str     x9, [x19, #SLOT_X_LR]
    ldr     x9, [x21, #FRAME_SP]
    str     x9, [x19, #SLOT_SP]
    ldr     x9, [x21, #FRAME_PC]
    str     x9, [x19, #SLOT_PC]
    ldr     w9, [x21, #FRAME_PSTATE]
    str     w9, [x19, #SLOT_PSTATE]
    str     x21, [x19, #SLOT_FRAME]
    ldr     w9, [x21, #FRAME_ESR]
    str     w9, [x19, #SLOT_ESR]
    ldr     x9, [x21, #FRAME_FAR]
    str     x9, [x19, #SLOT_FAR]

    /* x9-x30 from the parked registers. */
    ldp     x9,  x10, [sp, #0x00]
    stp     x9,  x10, [x19, #0x48]
    ldp     x11, x12, [sp, #0x10]
    stp     x11, x12, [x19, #0x58]
    ldp     x13, x14, [sp, #0x20]
    stp     x13, x14, [x19, #0x68]
    ldp     x15, x16, [sp, #0x30]
    stp     x15, x16, [x19, #0x78]
    ldp     x17, x18, [sp, #0x40]
    stp     x17, x18, [x19, #0x88]
    ldp     x9,  x10, [sp, #0x50]
    stp     x9,  x10, [x19, #0x98]
    ldp     x11, x12, [sp, #0x60]
    stp     x11, x12, [x19, #0xA8]
    ldp     x13, x14, [sp, #0x70]
    stp     x13, x14, [x19, #0xB8]
    ldp     x15, x16, [sp, #0x80]
    stp     x15, x16, [x19, #0xC8]
    ldp     x17, x18, [sp, #0x90]
    stp     x17, x18, [x19, #0xD8]
    ldp     x9,  x10, [sp, #0xA0]
    stp     x9,  x10, [x19, #0xE8]
    add     sp, sp, #0xB0

    /* Emulate on the slot's own stack. */
    mov     sp, x20
    ldr     x0, [x19, #SLOT_FAR]
    ldr     x1, [x19, #SLOT_ESR]
    ldr     x2, [x19, #SLOT_PC]
    mov     x3, x19
    bl      NsmbwFastmemFault
    cbz     x0, .Ldecline

    /* Resume where the handler says. */
    ldr     x9, [x19, #SLOT_FRAME]
    str     x0, [x9, #FRAME_PC]

    /* Restore q0-q31 and x0-x30, sp-relative off the x array. */
    add     sp, x19, #SLOT_X
    ldp     q0,  q1,  [sp, #-0x200]
    ldp     q2,  q3,  [sp, #-0x1E0]
    ldp     q4,  q5,  [sp, #-0x1C0]
    ldp     q6,  q7,  [sp, #-0x1A0]
    ldp     q8,  q9,  [sp, #-0x180]
    ldp     q10, q11, [sp, #-0x160]
    ldp     q12, q13, [sp, #-0x140]
    ldp     q14, q15, [sp, #-0x120]
    ldp     q16, q17, [sp, #-0x100]
    ldp     q18, q19, [sp, #-0x0E0]
    ldp     q20, q21, [sp, #-0x0C0]
    ldp     q22, q23, [sp, #-0x0A0]
    ldp     q24, q25, [sp, #-0x080]
    ldp     q26, q27, [sp, #-0x060]
    ldp     q28, q29, [sp, #-0x040]
    ldp     q30, q31, [sp, #-0x020]
    ldp     x0,  x1,  [sp, #0x00]
    ldp     x2,  x3,  [sp, #0x10]
    ldp     x4,  x5,  [sp, #0x20]
    ldp     x6,  x7,  [sp, #0x30]
    ldr     x8,       [sp, #0x40]
    ldp     x9,  x10, [sp, #0x48]
    ldp     x11, x12, [sp, #0x58]
    ldp     x13, x14, [sp, #0x68]
    ldp     x15, x16, [sp, #0x78]
    ldp     x17, x18, [sp, #0x88]
    ldp     x19, x20, [sp, #0x98]
    ldp     x21, x22, [sp, #0xA8]
    ldp     x23, x24, [sp, #0xB8]
    ldp     x25, x26, [sp, #0xC8]
    ldp     x27, x28, [sp, #0xD8]
    ldp     x29, x30, [sp, #0xE8]

    /* Release the slot last. x0-x8 come from the frame on resume, so they are
       the only registers still free; sp is slot + SLOT_X. */
    adrp    x0, g_fastmemSlots
    add     x0, x0, :lo12:g_fastmemSlots
    sub     x1, sp, x0
    sub     x1, x1, #SLOT_X
    lsr     x1, x1, #SLOT_SHIFT
    mov     w2, #1
    lsl     w2, w2, w1
    adrp    x3, g_fastmemSlotMask
    add     x3, x3, :lo12:g_fastmemSlotMask
.Lrelease:
    ldaxr   w4, [x3]
    bic     w4, w4, w2
    stxr    w5, w4, [x3]
    cbnz    w5, .Lrelease

    mov     w0, #0
    svc     #0x28
    /* Only reached when the kernel refused to resume. */
    bl      svcExitProcess

.Ldecline:
    ldr     x0, [x19, #SLOT_FAR]
    ldr     x1, [x19, #SLOT_ESR]
    ldr     x2, [x19, #SLOT_PC]
    bl      NsmbwFastmemUnhandled
    bl      svcExitProcess

.Lno_frame:
    adrp    x0, .Lno_frame_line
    add     x0, x0, :lo12:.Lno_frame_line
    mov     x1, #NO_FRAME_LEN
    bl      svcOutputDebugString
    bl      svcExitProcess

.Lslots_busy:
    adrp    x0, .Lslots_busy_line
    add     x0, x0, :lo12:.Lslots_busy_line
    mov     x1, #SLOTS_BUSY_LEN
    bl      svcOutputDebugString
    bl      svcExitProcess

.Lhang:
    b       .Lhang

    .size   __libnx_exception_entry, .-__libnx_exception_entry

    .section .rodata
.Lno_frame_line:
    .ascii  "[fastmem] exception entry without a frame\n"
    .equ    NO_FRAME_LEN, . - .Lno_frame_line
.Lslots_busy_line:
    .ascii  "[fastmem] no free exception slot\n"
    .equ    SLOTS_BUSY_LEN, . - .Lslots_busy_line

    .section .bss.g_fastmemSlots, "aw", %nobits
    .align  14
    .type   g_fastmemSlots, %object
    .size   g_fastmemSlots, SLOT_COUNT * SLOT_SIZE
g_fastmemSlots:
    .space  SLOT_COUNT * SLOT_SIZE

    .section .bss.g_fastmemSlotMask, "aw", %nobits
    .align  2
    .type   g_fastmemSlotMask, %object
    .size   g_fastmemSlotMask, 4
g_fastmemSlotMask:
    .space  4
