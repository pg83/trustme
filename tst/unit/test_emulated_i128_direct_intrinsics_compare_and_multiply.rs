//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
#![feature(core_intrinsics)]
#![allow(internal_features)]
use std::{cmp::Ordering, hint::black_box, intrinsics};
fn main() {
    let high = black_box((1u128 << 100) + 7);
    let low = black_box((1u128 << 64) + 11);
    assert_eq!(intrinsics::three_way_compare(high, low), Ordering::Greater);
    assert_eq!(intrinsics::three_way_compare(low, high), Ordering::Less);
    assert_eq!(intrinsics::three_way_compare(high, high), Ordering::Equal);
    let negative = black_box(-((1i128 << 100) + 7));
    let positive = black_box((1i128 << 64) + 11);
    assert_eq!(intrinsics::three_way_compare(negative, positive), Ordering::Less);
    assert_eq!(intrinsics::three_way_compare(positive, negative), Ordering::Greater);
    assert_eq!(intrinsics::three_way_compare(negative, negative), Ordering::Equal);
    assert_eq!(intrinsics::wrapping_mul(black_box(i128::MIN), black_box(-1)), i128::MIN);
    assert_eq!(intrinsics::wrapping_mul(black_box(u128::MAX), black_box(2)), u128::MAX - 1);
    let factor = black_box(-((1i128 << 60) + 3));
    let product = unsafe { intrinsics::unchecked_mul(factor, black_box(17)) };
    assert_eq!(product, -((1i128 << 64) + (1i128 << 60) + 51));
}
