//@ compile-flags: -Zmir-opt-level=3
#![feature(core_intrinsics, adt_const_params)]
#![allow(internal_features, incomplete_features)]
use std::marker::ConstParamTy;
#[derive(Clone, Copy, PartialEq, Eq, ConstParamTy)]
enum Choice { First, Second, Third }
#[inline(never)]
fn ordinary(value: u32) -> u32 {
    let mut sum = 0;
    let mut index = 0;
    while index < value { sum += index; index += 1; }
    sum
}
#[inline]
fn recursive(choice: Choice, value: u32) -> u32 {
    match choice {
        Choice::First => std::intrinsics::ctlz(value),
        Choice::Second => recursive(Choice::First, value),
        Choice::Third => std::intrinsics::ctpop(value),
    }
}
#[inline]
fn with_ordinary(choice: Choice, value: u32) -> u32 {
    match choice {
        Choice::First => std::intrinsics::ctlz(value),
        Choice::Second => ordinary(value),
        Choice::Third => std::intrinsics::ctpop(value),
    }
}
fn select<const C: Choice>(value: u32) -> (u32, u32) { (recursive(C, value), with_ordinary(C, value)) }
fn main() {
    assert_eq!(select::<{Choice::First}>(128), (24, 24));
    assert_eq!(select::<{Choice::Second}>(4), (29, 6));
    assert_eq!(select::<{Choice::Third}>(129), (2, 2));
}
