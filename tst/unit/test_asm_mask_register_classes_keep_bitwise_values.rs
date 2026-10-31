#[target_feature(enable = "avx512f")]
unsafe fn combine() -> u16 {
    let output: u16;
    unsafe { std::arch::asm!("korw {output}, {left}, {right}", output = lateout(kreg) output, left = in(kreg) 0x37u16, right = in(kreg) 0x1234u16, options(nomem, nostack)); }
    output
}
#[target_feature(enable = "avx512f")]
unsafe fn invert() -> u16 {
    let mut value = 0x37u16;
    unsafe { std::arch::asm!("knotw k1, k1", inout("k1") value, options(nomem, nostack)); }
    value
}
fn main() {
    if std::is_x86_feature_detected!("avx512f") {
        assert_eq!(unsafe { combine() }, 0x1237);
        assert_eq!(unsafe { invert() }, 0xffc8);
    }
}
