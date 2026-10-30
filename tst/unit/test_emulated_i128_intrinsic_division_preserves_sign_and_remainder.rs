//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
#![feature(core_intrinsics)]
#![allow(internal_features)]
use std::hint::black_box;
fn unsigned(value: u128, divisor: u128) -> (u128, u128) {
    unsafe { (std::intrinsics::unchecked_div(value, divisor), std::intrinsics::unchecked_rem(value, divisor)) }
}
fn signed(value: i128, divisor: i128) -> (i128, i128) {
    unsafe { (std::intrinsics::unchecked_div(value, divisor), std::intrinsics::unchecked_rem(value, divisor)) }
}
fn main() {
    let quotient = (1u128 << 63) + 5;
    let divisor = (1u128 << 64) + 7;
    let (q, r) = unsigned(black_box(quotient * divisor + 17), black_box(divisor));
    assert!(q == quotient && r == 17);
    let exact = unsafe { std::intrinsics::exact_div(black_box(quotient * divisor), black_box(divisor)) };
    assert!(exact == quotient);
    let quotient = (1i128 << 72) + 11;
    let divisor = (1i128 << 36) + 9;
    let numerator = quotient * divisor + 19;
    let (q, r) = signed(black_box(-numerator), black_box(divisor));
    assert!(q == -quotient && r == -19);
    let (q, r) = signed(black_box(numerator), black_box(-divisor));
    assert!(q == -quotient && r == 19);
    let (q, r) = signed(black_box(-numerator), black_box(-divisor));
    assert!(q == quotient && r == -19);
    let exact = unsafe { std::intrinsics::exact_div(black_box(-quotient * divisor), black_box(divisor)) };
    assert!(exact == -quotient);
}
