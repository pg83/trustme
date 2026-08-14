#![feature(core_intrinsics, repr_simd)]

use std::intrinsics::simd::simd_bitmask;

#[repr(simd)]
#[derive(Copy, Clone)]
struct U8x4([u8; 4]);

#[repr(simd)]
#[derive(Copy, Clone)]
struct Usizex4([usize; 4]);

#[repr(simd)]
#[derive(Copy, Clone)]
struct Isizex4([isize; 4]);

fn main() {
    unsafe {
        let byte_mask: u8 = simd_bitmask(U8x4([0x80, 0x41, 0xc1, 0xff]));
        assert_eq!(byte_mask, 0b1101);

        let usize_mask: u8 = simd_bitmask(Usizex4([usize::MAX, 0, usize::MAX, usize::MAX]));
        assert_eq!(usize_mask, 0b1101);

        let isize_mask: u8 = simd_bitmask(Isizex4([-1, 0, isize::MIN, 1]));
        assert_eq!(isize_mask, 0b0101);
    }
}
