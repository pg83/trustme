#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Family { type Item; }
impl Family for u8 { type Item = u16; }
struct Holder<T>(PhantomData<T>);
trait Rank<T> { type Output; fn code() -> u8; }
impl<T, U> Rank<T> for Holder<U> {
    default type Output = u32;
    default fn code() -> u8 { 1 }
}
impl<T: Family> Rank<T> for Holder<T::Item> {
    default fn code() -> u8 { 2 }
}
impl<T: Family<Item = u16>> Rank<T> for Holder<u16> {
    fn code() -> u8 { 3 }
}
fn main() {
    assert_eq!(<Holder<u16> as Rank<u8>>::code(), 3);
    assert_eq!(<Holder<u32> as Rank<u8>>::code(), 1);
    let _: PhantomData<<Holder<u16> as Rank<u8>>::Output> = PhantomData::<u32>;
}
