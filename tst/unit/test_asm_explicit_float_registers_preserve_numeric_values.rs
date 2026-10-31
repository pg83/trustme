fn main() {
    let mut value = 7.5f64;
    let step = 5.0f64;
    let zero: f32;
    unsafe {
        std::arch::asm!("addsd xmm0, xmm1", inout("xmm0") value, in("xmm1") step, options(nomem, nostack));
        std::arch::asm!("xorps xmm0, xmm0", out("xmm0") zero, options(nomem, nostack));
    }
    assert_eq!(value, 12.5);
    assert_eq!(zero, 0.0);
}
