use std::arch::x86_64::__m128i;
fn main() {
    let mut value: __m128i = unsafe { std::mem::transmute([1u32, 3, 5, 7]) };
    let step: __m128i = unsafe { std::mem::transmute([17u32, 19, 23, 29]) };
    let copied: __m128i;
    unsafe {
        std::arch::asm!("paddd xmm0, xmm1", inout("xmm0") value, in("xmm1") step, options(nomem, nostack));
        std::arch::asm!("movdqa {copied:x}, {value:x}", copied = lateout(xmm_reg) copied, value = in(xmm_reg) value, options(nomem, nostack));
    }
    let lanes: [u32; 4] = unsafe { std::mem::transmute(value) };
    assert_eq!(lanes, [18, 22, 28, 36]);
    let copied: [u32; 4] = unsafe { std::mem::transmute(copied) };
    assert_eq!(copied, lanes);
}
