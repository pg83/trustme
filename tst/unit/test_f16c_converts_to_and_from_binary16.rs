// llvm.x86.vcvtps2ph.128/256 (_mm_cvtps_ph, _mm256_cvtps_ph) and
// llvm.x86.vcvtph2ps.128/256 (_mm_cvtph_ps, _mm256_cvtph_ps) had no lowering
// and aborted on `assert(!"Extern LLVM: ...")`; half's binary16 conversions
// use them. VCVTPS2PH narrows each f32 lane to binary16 with the rounding
// imm8[1:0] names (nearest-even, down, up, toward zero), or MXCSR's when
// imm8[2] is set, and zeroes the upper half of a 128-bit result; VCVTPH2PS
// widens the low four (or all eight) binary16 lanes exactly (Intel SDM).
#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::*;

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "f16c")]
unsafe fn check() -> bool {
    let a = _mm_setr_ps(1.0, 1.0009765625 + 0.0004, -1.0009765625 - 0.0004, 65536.0);
    let nearest: [u16; 8] = core::mem::transmute(_mm_cvtps_ph::<0>(a));
    let down: [u16; 8] = core::mem::transmute(_mm_cvtps_ph::<1>(a));
    let up: [u16; 8] = core::mem::transmute(_mm_cvtps_ph::<2>(a));
    let zero: [u16; 8] = core::mem::transmute(_mm_cvtps_ph::<3>(a));
    let mxcsr: [u16; 8] = core::mem::transmute(_mm_cvtps_ph::<4>(a));
    let wide = _mm256_setr_ps(0.5, -2.0, 3.0, 0.1, 1.0, 2.0, 4.0, 8.0);
    let narrowed: [u16; 8] = core::mem::transmute(_mm256_cvtps_ph::<0>(wide));
    nearest == [0x3c00, 0x3c01, 0xbc01, 0x7c00, 0, 0, 0, 0]
        && down == [0x3c00, 0x3c01, 0xbc02, 0x7bff, 0, 0, 0, 0]
        && up == [0x3c00, 0x3c02, 0xbc01, 0x7c00, 0, 0, 0, 0]
        && zero == [0x3c00, 0x3c01, 0xbc01, 0x7bff, 0, 0, 0, 0]
        && mxcsr == nearest
        && narrowed == [0x3800, 0xc000, 0x4200, 0x2e66, 0x3c00, 0x4000, 0x4400, 0x4800]
        && {
            let halves = _mm_setr_epi16(0x3c00, 0xc000u16 as i16, 0x7c00, 0x0001, 0x3800, 0x7bff, -0x8000, 0x4200);
            let low: [f32; 4] = core::mem::transmute(_mm_cvtph_ps(halves));
            let all: [f32; 8] = core::mem::transmute(_mm256_cvtph_ps(halves));
            low == [1.0, -2.0, f32::INFINITY, 5.9604645e-8]
                && all == [1.0, -2.0, f32::INFINITY, 5.9604645e-8, 0.5, 65504.0, -0.0, 3.0]
                && all[6].is_sign_negative()
        }
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("f16c") && std::is_x86_feature_detected!("avx") {
        assert!(unsafe { check() });
        return;
    }
    println!("skip");
}
