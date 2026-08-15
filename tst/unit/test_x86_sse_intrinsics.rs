#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::*;

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "sse")]
unsafe fn test_sse() {
    let reciprocal: [f32; 4] = core::mem::transmute(_mm_rcp_ss(_mm_setr_ps(4.0, 2.0, 3.0, 4.0)));
    assert!((reciprocal[0] - 0.25).abs() < 0.001);
    assert_eq!(&reciprocal[1..], &[2.0, 3.0, 4.0]);

    let root: [f32; 4] = core::mem::transmute(_mm_rsqrt_ps(_mm_setr_ps(4.0, 16.0, 64.0, 256.0)));
    assert!((root[0] - 0.5).abs() < 0.001);
    assert!((root[3] - 0.0625).abs() < 0.001);

    let minimum: [u32; 4] = core::mem::transmute(_mm_min_ps(
        _mm_set1_ps(1.0),
        _mm_setr_ps(f32::NAN, -0.0, 2.0, -2.0),
    ));
    assert_eq!(minimum[0], f32::NAN.to_bits());
    assert_eq!(minimum[1], (-0.0_f32).to_bits());

    assert_eq!(_mm_cvtss_si32(_mm_set_ss(-33.5)), -34);
    assert_eq!(_mm_cvttss_si32(_mm_set_ss(-33.5)), -33);
    assert_eq!(_mm_cvttss_si32(_mm_set_ss(f32::NAN)), i32::MIN);
    assert_eq!(_mm_comineq_ss(_mm_set_ss(f32::NAN), _mm_set_ss(1.0)), 1);

    let converted: [f32; 4] = core::mem::transmute(_mm_cvtsi64_ss(_mm_setr_ps(1.0, 2.0, 3.0, 4.0), 5));
    assert_eq!(converted, [5.0, 2.0, 3.0, 4.0]);
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        test_sse();
    }
}
