//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
//@ output-contains: .cpp #define TRUSTME_TARGET_EMULATED_I128 1

use std::hint::black_box;

fn main() {
    let maximum = black_box(u128::MAX);
    let minimum = black_box(i128::MIN);
    let one = black_box(1u128);
    let high = black_box(1u128 << 100);
    let (value, overflow) = maximum.overflowing_add(one);
    assert!(value == 0 && overflow);
    let (value, overflow) = one.overflowing_sub(2);
    assert!(value == maximum && overflow);
    let (value, overflow) = high.overflowing_mul(1 << 30);
    assert!(value == 0 && overflow);
    let (value, overflow) = minimum.overflowing_add(-1);
    assert!(value == i128::MAX && overflow);
    let (value, overflow) = minimum.overflowing_sub(1);
    assert!(value == i128::MAX && overflow);
    let (value, overflow) = minimum.overflowing_mul(-1);
    assert!(value == minimum && overflow);
    assert!(maximum.wrapping_add(2) == 1);
    assert!(one.wrapping_sub(3) == maximum - 1);
    assert!(maximum.wrapping_mul(2) == maximum - 1);
    assert!(maximum.saturating_add(one) == maximum);
    assert!(one.saturating_sub(3) == 0);
    assert!(minimum.saturating_add(-1) == minimum);
    assert!(minimum.saturating_sub(1) == minimum);
    assert!(minimum.saturating_mul(-1) == i128::MAX);
    assert!(high.leading_zeros() == 27);
    assert!(high.trailing_zeros() == 100);
    assert!(high.count_ones() == 1);
    assert!(maximum.count_ones() == 128);
    assert!(one.rotate_left(127) == 1u128 << 127);
    assert!(one.rotate_right(1) == 1u128 << 127);
    assert!(one.swap_bytes() == 1u128 << 120);
    assert!(one.reverse_bits() == 1u128 << 127);
    assert!(minimum.unsigned_abs() == 1u128 << 127);
}
