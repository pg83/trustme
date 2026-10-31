#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
pub trait Family { type Item; }
impl Family for u8 { type Item = u16; }
pub struct Holder<T>(pub PhantomData<T>);
pub trait Rank<T, U> { type Output; fn code() -> u8; }
impl<T: Family<Item = u16>> Rank<T, u8> for Holder<u16> { fn code() -> u8 { 3 } }
impl<T: Family, U> Rank<T, U> for Holder<T::Item> {
    default type Output = u32;
    default fn code() -> u8 { 1 }
}
impl<T: Family<Item = u16>, U> Rank<T, U> for Holder<u16> {
    default type Output = u64;
    default fn code() -> u8 { 2 }
}
