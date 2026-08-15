#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::*;

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "ssse3")]
unsafe fn test_sse3_ssse3() {
    let absolute = _mm_abs_epi8(_mm_setr_epi8(
        i8::MIN, -5, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    ));
    assert_eq!(
        core::mem::transmute::<_, [i8; 16]>(absolute)[..4],
        [i8::MIN, 5, 7, 0],
    );

    let floats = _mm_hadd_ps(
        _mm_setr_ps(1.0, 2.0, 4.0, 8.0),
        _mm_setr_ps(16.0, 32.0, 64.0, 128.0),
    );
    assert_eq!(core::mem::transmute::<_, [f32; 4]>(floats), [3.0, 12.0, 48.0, 192.0]);

    let doubles = _mm_cmpeq_pd(_mm_setr_pd(1.0, 2.0), _mm_setr_pd(1.0, 3.0));
    assert_eq!(core::mem::transmute::<_, [u64; 2]>(doubles), [u64::MAX, 0]);

    let source = _mm_setr_epi8(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    assert_eq!(
        core::mem::transmute::<_, [i8; 16]>(_mm_lddqu_si128(&source)),
        core::mem::transmute::<_, [i8; 16]>(source),
    );

    let saturated = _mm_hadds_epi16(
        _mm_setr_epi16(i16::MAX, 1, 10, 20, -10, -20, i16::MIN, -1),
        _mm_setzero_si128(),
    );
    assert_eq!(
        core::mem::transmute::<_, [i16; 8]>(saturated),
        [i16::MAX, 30, -30, i16::MIN, 0, 0, 0, 0],
    );

    let rounded = _mm_mulhrs_epi16(
        _mm_setr_epi16(i16::MAX, i16::MIN, 0, 0, 0, 0, 0, 0),
        _mm_setr_epi16(i16::MAX, i16::MIN, 0, 0, 0, 0, 0, 0),
    );
    assert_eq!(
        core::mem::transmute::<_, [i16; 8]>(rounded),
        [i16::MAX - 1, i16::MIN, 0, 0, 0, 0, 0, 0],
    );

    let signed = _mm_sign_epi8(
        _mm_setr_epi8(1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        _mm_setr_epi8(1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    );
    assert_eq!(core::mem::transmute::<_, [i8; 16]>(signed)[..3], [1, -2, 0]);
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        test_sse3_ssse3();
    }
}
