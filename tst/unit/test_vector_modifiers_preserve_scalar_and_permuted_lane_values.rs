//@ compile-variant: -O
use std::arch::x86_64::{__m256i, __m512i};
#[target_feature(enable = "avx")]
unsafe fn halves() -> [u64; 4] {
    let mut lanes: __m256i = std::mem::transmute([401u64, 409, 419, 421]);
    core::arch::asm!("vperm2f128 {value:y}, {value:y}, {value:y}, 1", "/* {value:x} {value:z} */",
        value = inout(ymm_reg) lanes, options(nomem, nostack));
    std::mem::transmute(lanes)
}
#[target_feature(enable = "avx512f")]
unsafe fn quarters() -> [u64; 8] {
    let mut lanes: __m512i = std::mem::transmute([431u64, 433, 439, 443, 449, 457, 461, 463]);
    core::arch::asm!("vpermpd {value:z}, {value:z}, 0x4e", "/* {value:x} {value:y} */",
        value = inout(zmm_reg) lanes, options(nomem, nostack));
    std::mem::transmute(lanes)
}
fn main() {
    let mut scalar = 2.5f64;
    unsafe {
        core::arch::asm!("addsd {value:x}, {other:x}", "/* {value:y} {value:z} */",
            value = inout(xmm_reg) scalar, other = in(xmm_reg) 1.75f64,
            options(nomem, nostack));
    }
    assert_eq!(scalar, 4.25);
    std::hint::black_box(halves as unsafe fn() -> [u64; 4]);
    std::hint::black_box(quarters as unsafe fn() -> [u64; 8]);
    if std::is_x86_feature_detected!("avx") { assert_eq!(unsafe { halves() }, [419, 421, 401, 409]); }
    if std::is_x86_feature_detected!("avx512f") { assert_eq!(unsafe { quarters() }, [439, 443, 431, 433, 461, 463, 449, 457]); }
}
