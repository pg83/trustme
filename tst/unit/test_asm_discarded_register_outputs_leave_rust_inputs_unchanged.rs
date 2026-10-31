use std::arch::x86_64::__m128i;
fn main() {
    let scalar = 37u64;
    let vector: __m128i = unsafe { std::mem::transmute([17u32, 23, 41, 53]) };
    let mut explicit = 37u64;
    unsafe {
        std::arch::asm!("inc {value}", value = inout(reg) scalar => _, options(nomem, nostack));
        std::arch::asm!("pxor {value}, {value}", value = inout(xmm_reg) vector => _, options(nomem, nostack));
        std::arch::asm!("lea r10, [r10 + 5]", inout("r10") explicit, options(nomem, nostack, preserves_flags));
        std::arch::asm!("mov {scratch}, 37", scratch = out(reg) _, options(nomem, nostack));
    }
    assert_eq!(scalar, 37);
    assert_eq!(explicit, 42);
    let lanes: [u32; 4] = unsafe { std::mem::transmute(vector) };
    assert_eq!(lanes, [17, 23, 41, 53]);
}
