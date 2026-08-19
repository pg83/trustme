//@ run-pass
// A vector register class takes a vector, and the C type a SIMD value is
// emitted as is a struct -- which the compiler will not put in an `xmm`
// register. Such an operand goes through a vector of the same width instead.

#![cfg(target_arch = "x86_64")]

use core::arch::x86_64::{__m128, __m128i, _mm_set_epi64x};

fn main() {
    let value: __m128i = unsafe { _mm_set_epi64x(2, 1) };
    unsafe {
        core::arch::asm!("/* {} */", in(xmm_reg) value);
    }

    // A scalar still goes straight in.
    let scalar = 1.0f32;
    unsafe {
        core::arch::asm!("/* {} */", in(xmm_reg) scalar);
    }

    // swizzle [0, 1, 2, 3] => [3, 2, 0, 1]
    const SHUFFLE: u8 = 0b01_00_10_11;
    let lanes: __m128 = unsafe { core::mem::transmute([0u32, 1u32, 2u32, 3u32]) };
    let swizzled: __m128;
    unsafe {
        core::arch::asm!(
            "pshufd {xmm}, {xmm}, {shuffle}",
            xmm = inlateout(xmm_reg) lanes => swizzled,
            shuffle = const SHUFFLE,
        );
    }
    let swizzled: [u32; 4] = unsafe { core::mem::transmute(swizzled) };
    assert_eq!(swizzled, [3, 2, 0, 1]);
}
