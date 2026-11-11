#![feature(intrinsics, f16, f128)]
#![allow(internal_features)]

#[rustc_intrinsic]
unsafe fn rintf16(value: f16) -> f16;
#[rustc_intrinsic]
unsafe fn rintf32(value: f32) -> f32;
#[rustc_intrinsic]
unsafe fn rintf64(value: f64) -> f64;
#[rustc_intrinsic]
unsafe fn rintf128(value: f128) -> f128;
#[rustc_intrinsic]
unsafe fn maxnumf128(left: f128, right: f128) -> f128;
#[rustc_intrinsic]
unsafe fn minnumf128(left: f128, right: f128) -> f128;

fn main() {
    use std::hint::black_box;

    unsafe {
        assert!(rintf16(black_box(2.5f16)) == 2.0f16);
        assert!(rintf32(black_box(-2.5f32)) == -2.0f32);
        assert!(rintf64(black_box(3.5f64)) == 4.0f64);
        assert!(rintf128(black_box(-3.5f128)) == -4.0f128);
        assert!(rintf16(black_box(-2.5f16)) == -2.0f16);
        assert!(rintf32(black_box(3.5f32)) == 4.0f32);
        assert!(rintf64(black_box(-2.5f64)) == -2.0f64);
        assert!(rintf128(black_box(2.5f128)) == 2.0f128);

        let larger = black_box(3.5f128);
        let smaller = black_box(-9.25f128);
        let nan = black_box(f128::NAN);
        assert!(maxnumf128(larger, smaller) == larger);
        assert!(minnumf128(larger, smaller) == smaller);
        assert!(maxnumf128(nan, smaller) == smaller);
        assert!(maxnumf128(larger, nan) == larger);
        assert!(minnumf128(nan, smaller) == smaller);
        assert!(minnumf128(larger, nan) == larger);
    }
}
