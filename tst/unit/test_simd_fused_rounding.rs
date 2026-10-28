#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]

#[repr(simd)]
#[derive(Clone, Copy)]
struct Singles([f32; 4]);

#[repr(simd)]
#[derive(Clone, Copy)]
struct Doubles([f64; 2]);

#[inline(never)]
fn fused(a: Singles, b: Singles, c: Singles) -> Singles {
    unsafe { core::intrinsics::simd::simd_fma(a, b, c) }
}

fn main() {
    let near_one = f32::from_bits(0x3f80_0800);
    let tiny = f32::from_bits(67 << 23);
    let result = fused(
        Singles([near_one, near_one, 3.0, -4.0]),
        Singles([near_one, near_one, 2.0, 0.5]),
        Singles([tiny, -tiny, -5.0, 3.0]),
    );
    assert_eq!(result.0[0].to_bits(), 0x3f80_1001);
    assert_eq!(result.0[1].to_bits(), 0x3f80_1000);
    assert_eq!(result.0[2], 1.0);
    assert_eq!(result.0[3], 1.0);

    let a = std::hint::black_box(Doubles([1.0 + 1.0 / (1u64 << 27) as f64, 7.0]));
    let b = std::hint::black_box(Doubles([1.0 - 1.0 / (1u64 << 27) as f64, 0.5]));
    let c = std::hint::black_box(Doubles([-1.0, 0.25]));
    let double: Doubles = unsafe { core::intrinsics::simd::simd_fma(a, b, c) };
    assert_eq!(double.0, [-1.0 / (1u64 << 54) as f64, 3.75]);
}
