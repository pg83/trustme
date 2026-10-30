//@ compile-flags: -Zmir-opt-level=3
#![feature(core_intrinsics, adt_const_params)]
#![allow(internal_features, incomplete_features)]
use std::marker::ConstParamTy;
#[derive(Clone, Copy, PartialEq, Eq, ConstParamTy)]
enum Choice { Leading, Trailing, Population }
#[inline]
fn count(choice: Choice, value: u32) -> u32 {
    match choice {
        Choice::Leading => std::intrinsics::ctlz(value),
        Choice::Trailing => std::intrinsics::cttz(value),
        Choice::Population => std::intrinsics::ctpop(value),
    }
}
fn constant<const CHOICE: Choice>(value: u32) -> u32 { count(CHOICE, value) }
fn main() {
    assert_eq!(constant::<{ Choice::Leading }>(128), 24);
    assert_eq!(constant::<{ Choice::Trailing }>(128), 7);
    assert_eq!(constant::<{ Choice::Population }>(129), 2);
}
