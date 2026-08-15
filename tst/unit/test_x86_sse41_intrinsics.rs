#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::*;

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "sse4.1")]
unsafe fn test_sse41() {
    let inserted: [f32; 4] = core::mem::transmute(_mm_insert_ps::<0b11_00_1100>(
        _mm_set1_ps(1.0),
        _mm_setr_ps(1.0, 2.0, 3.0, 4.0),
    ));
    assert_eq!(inserted, [4.0, 1.0, 0.0, 0.0]);

    let packed: [u16; 8] = core::mem::transmute(_mm_packus_epi32(
        _mm_setr_epi32(i32::MIN, -1, 0, 65535),
        _mm_setr_epi32(1, 65536, i32::MAX, 7),
    ));
    assert_eq!(packed, [0, 0, 0, u16::MAX, 1, u16::MAX, u16::MAX, 7]);

    let dot_ps: [f32; 4] = core::mem::transmute(_mm_dp_ps::<0b0111_0101>(
        _mm_setr_ps(2.0, 3.0, 1.0, 10.0),
        _mm_setr_ps(1.0, 4.0, 0.5, 10.0),
    ));
    assert_eq!(dot_ps, [14.5, 0.0, 14.5, 0.0]);
    let dot_pd: [f64; 2] = core::mem::transmute(_mm_dp_pd::<0b0011_0001>(
        _mm_setr_pd(2.0, 3.0),
        _mm_setr_pd(1.0, 4.0),
    ));
    assert_eq!(dot_pd, [14.0, 0.0]);

    let rounded: [f32; 4] = core::mem::transmute(_mm_round_ps::<_MM_FROUND_TO_NEAREST_INT>(
        _mm_setr_ps(1.5, 2.5, -1.5, -2.5),
    ));
    assert_eq!(rounded, [2.0, 2.0, -2.0, -2.0]);
    let scalar: [f64; 2] = core::mem::transmute(_mm_round_sd::<_MM_FROUND_TO_NEG_INF>(
        _mm_setr_pd(10.0, 20.0),
        _mm_setr_pd(-1.1, 30.0),
    ));
    assert_eq!(scalar, [-2.0, 20.0]);

    let minimum: [u16; 8] = core::mem::transmute(_mm_minpos_epu16(core::mem::transmute([
        23_u16, 13, 44, 97, 50, 13, 67, 66,
    ])));
    assert_eq!(minimum, [13, 1, 0, 0, 0, 0, 0, 0]);

    let bytes: __m128i = core::mem::transmute([0_u8, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
    let sums: [u16; 8] = core::mem::transmute(_mm_mpsadbw_epu8::<0b001>(bytes, bytes));
    assert_eq!(sums, [16, 12, 8, 4, 0, 4, 8, 12]);

    let value = _mm_set1_epi8(0b101);
    let mask = _mm_set1_epi8(0b110);
    assert_eq!(_mm_testz_si128(value, mask), 0);
    assert_eq!(_mm_testc_si128(value, mask), 0);
    assert_eq!(_mm_testnzc_si128(value, mask), 1);
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        test_sse41();
    }
}
