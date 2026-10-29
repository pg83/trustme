#![feature(repr_simd, core_intrinsics)]
#[repr(simd)]
#[derive(Clone, Copy)]
struct Signed([i32; 4]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Float([f32; 4]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Byte([u8; 4]);
fn main() {
    unsafe {
        let floats: Float = std::intrinsics::simd::simd_cast(std::hint::black_box(Signed([-7, 0, 13, 257])));
        assert_eq!(floats.0, [-7.0, 0.0, 13.0, 257.0]);
        let integers: Signed = std::intrinsics::simd::simd_cast(std::hint::black_box(Float([-3.5, 2.25, 0.0, 19.75])));
        assert_eq!(integers.0, [-3, 2, 0, 19]);
        let bytes: Byte = std::intrinsics::simd::simd_cast(std::hint::black_box(Signed([-1, 256, 258, 511])));
        assert_eq!(bytes.0, [255, 0, 2, 255]);
    }
}
