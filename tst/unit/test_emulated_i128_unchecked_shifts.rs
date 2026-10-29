//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
//@ output-contains: .cpp #define TRUSTME_TARGET_EMULATED_I128 1
#![feature(core_intrinsics)]
use std::hint::black_box;
fn main() {
    unsafe {
        assert_eq!(std::intrinsics::unchecked_shl(black_box(3u128), black_box(65u128)), 3u128 << 65);
        assert_eq!(std::intrinsics::unchecked_shr(black_box(5u128 << 96), black_box(96u32)), 5);
        assert_eq!(std::intrinsics::unchecked_shl(black_box(-3i128), black_box(7u32)), -384);
        assert_eq!(std::intrinsics::unchecked_shr(black_box(-9i128 << 70), black_box(70u128)), -9);
    }
}
