#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::*;

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "sse2")]
unsafe fn test_sse2() {
    let count = _mm_cvtsi64_si128(4);
    let words: __m128i = core::mem::transmute([0x0001_u16, 0x8001, 0xffff, 7, 8, 9, 10, 11]);
    let shifted: [u16; 8] = core::mem::transmute(_mm_sll_epi16(words, count));
    assert_eq!(shifted, [0x0010, 0x0010, 0xfff0, 0x0070, 0x0080, 0x0090, 0x00a0, 0x00b0]);

    let dwords: __m128i = core::mem::transmute([-16_i32, 16, i32::MIN, i32::MAX]);
    let arithmetic: [i32; 4] = core::mem::transmute(_mm_sra_epi32(dwords, count));
    assert_eq!(arithmetic, [-1, 1, -134_217_728, 134_217_727]);

    let packed: [i16; 8] = core::mem::transmute(_mm_packs_epi32(
        core::mem::transmute([i32::MIN, -32768, 32767, i32::MAX]),
        core::mem::transmute([-1_i32, 0, 1, 65535]),
    ));
    assert_eq!(packed, [i16::MIN, i16::MIN, i16::MAX, i16::MAX, -1, 0, 1, i16::MAX]);

    let rounded: [i32; 4] = core::mem::transmute(_mm_cvtpd_epi32(_mm_setr_pd(-33.5, 1.5)));
    assert_eq!(rounded, [-34, 2, 0, 0]);
    let truncated: [i32; 4] = core::mem::transmute(_mm_cvttps_epi32(_mm_setr_ps(-33.5, 1.9, f32::NAN, 4.0e10)));
    assert_eq!(truncated, [-33, 1, i32::MIN, i32::MIN]);
    assert_eq!(_mm_cvtsd_si64(_mm_set_sd(-33.5)), -34);
    assert_eq!(_mm_cvttsd_si32(_mm_set_sd(-33.5)), -33);

    let widened: [f64; 2] = core::mem::transmute(_mm_cvtss_sd(_mm_setr_pd(1.0, 2.0), _mm_set_ss(5.5)));
    assert_eq!(widened, [5.5, 2.0]);
    let narrowed: [f32; 4] = core::mem::transmute(_mm_cvtsd_ss(_mm_setr_ps(1.0, 2.0, 3.0, 4.0), _mm_set_sd(5.5)));
    assert_eq!(narrowed, [5.5, 2.0, 3.0, 4.0]);

    let minimum: [u64; 2] = core::mem::transmute(_mm_min_pd(
        _mm_set1_pd(1.0),
        _mm_setr_pd(f64::NAN, -0.0),
    ));
    assert_eq!(minimum, [f64::NAN.to_bits(), (-0.0_f64).to_bits()]);
    assert_eq!(_mm_ucomineq_sd(_mm_set_sd(f64::NAN), _mm_set_sd(1.0)), 1);
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        test_sse2();
    }
}
