#![feature(repr_simd, platform_intrinsics)]
#[repr(simd)]
#[derive(Clone, Copy)]
struct Four([u16; 4]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Two([u16; 2]);
extern "platform-intrinsic" {
    fn simd_shuffle4<T, U>(left: T, right: T, indices: [u32; 4]) -> U;
    fn simd_shuffle2<T, U>(left: T, right: T, indices: [u32; 2]) -> U;
}
fn main() {
    let left = std::hint::black_box(Four([1, 2, 3, 4]));
    let right = std::hint::black_box(Four([11, 12, 13, 14]));
    unsafe {
        let mixed: Four = simd_shuffle4(left, right, [7, 0, 5, 2]);
        assert_eq!(mixed.0, [14, 1, 12, 3]);
        let pair: Two = simd_shuffle2(left, right, [3, 4]);
        assert_eq!(pair.0, [4, 11]);
    }
}
