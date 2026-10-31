#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Family { type Item; }
trait Narrow: Family<Item = u16> {}
impl Family for u8 { type Item = u16; }
impl Narrow for u8 {}
struct Holder<T>(PhantomData<T>);
trait Rank<T, U> { type Output; fn code() -> u8; }
impl<T: Narrow> Rank<T, u8> for Holder<u16> { fn code() -> u8 { 3 } }
impl<T: Family, U> Rank<T, U> for Holder<T::Item> {
    default type Output = u32;
    default fn code() -> u8 { 1 }
}
impl<T: Narrow, U> Rank<T, U> for Holder<u16> {
    default type Output = u64;
    default fn code() -> u8 { 2 }
}
fn main() {
    assert_eq!(<Holder<u16> as Rank<u8, u8>>::code(), 3);
    let _: PhantomData<<Holder<u16> as Rank<u8, u8>>::Output> = PhantomData::<u64>;
}
