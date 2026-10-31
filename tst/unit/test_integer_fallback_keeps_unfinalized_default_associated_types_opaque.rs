//@ compile-fail: Failed to find an impl
#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Rank { type Output; fn code() -> u8; }
impl Rank for u8 { fn code() -> u8 { 3 } }
impl<T: Copy> Rank for T {
    default type Output = u32;
    default fn code() -> u8 { 1 }
}
impl<T: Copy + Send> Rank for T {
    default type Output = u64;
    default fn code() -> u8 { 2 }
}
impl Rank for u16 { fn code() -> u8 { 4 } }
fn resolve<T: Rank>(value: T) -> (T, PhantomData<T::Output>, u8) {
    (value, PhantomData, T::code())
}
fn main() {
    let result: (_, PhantomData<u64>, _) = resolve(0);
    assert_eq!(result.0, 0i32);
    assert_eq!(result.2, 2);
}
