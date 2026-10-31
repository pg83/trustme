use std::arch::x86_64::{__m256i, __m512i};
#[target_feature(enable = "avx2")]
unsafe fn double() -> [u32; 8] {
    let mut value: __m256i = unsafe { std::mem::transmute([1u32, 2, 3, 4, 5, 6, 7, 8]) };
    let copied: __m256i;
    unsafe { std::arch::asm!("vpslld {value:y}, {value:y}, 1", value = inout(ymm_reg) value, options(nomem, nostack)); }
    unsafe { std::arch::asm!("vmovdqa {copied:y}, {value:y}", copied = lateout(ymm_reg) copied, value = in(ymm_reg) value, options(nomem, nostack)); }
    unsafe { std::mem::transmute(copied) }
}
#[target_feature(enable = "avx512f")]
unsafe fn halve() -> [u32; 16] {
    let input: __m512i = unsafe { std::mem::transmute([6u32; 16]) };
    let output: __m512i;
    unsafe { std::arch::asm!("vpsrld {output:z}, {input:z}, 1", output = lateout(zmm_reg) output, input = in(zmm_reg) input, options(nomem, nostack)); }
    unsafe { std::mem::transmute(output) }
}
#[target_feature(enable = "avx512f")]
unsafe fn zero_explicit() -> [u32; 16] {
    let mut value: __m512i = unsafe { std::mem::transmute([37u32; 16]) };
    unsafe { std::arch::asm!("vpxord zmm2, zmm2, zmm2", inout("zmm2") value, options(nomem, nostack)); }
    unsafe { std::mem::transmute(value) }
}
fn main() {
    if std::is_x86_feature_detected!("avx2") { assert_eq!(unsafe { double() }, [2, 4, 6, 8, 10, 12, 14, 16]); }
    if std::is_x86_feature_detected!("avx512f") {
        assert_eq!(unsafe { halve() }, [3; 16]);
        assert_eq!(unsafe { zero_explicit() }, [0; 16]);
    }
}
