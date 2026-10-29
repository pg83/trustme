#![feature(core_intrinsics, f16)]
use std::hint::black_box;
fn main() {
    unsafe {
        assert_eq!(std::intrinsics::powf32(black_box(4.0f32), black_box(0.5f32)), 2.0);
        assert_eq!(std::intrinsics::powf64(black_box(2.0f64), black_box(-3.0f64)), 0.125);
        assert!(std::intrinsics::powf16(black_box(9.0f16), black_box(0.5f16)) == 3.0f16);
    }
}
