#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Family { type Item; }
impl Family for u8 { type Item = u16; }
struct Group<T>(PhantomData<T>);
trait Rank<A, B> { const WIDTH: usize; fn code() -> u8; }
impl<T: Family, U> Rank<T::Item, U> for Group<T> {
    default const WIDTH: usize = 3;
    default fn code() -> u8 { 1 }
}
impl<T: Family> Rank<T::Item, T::Item> for Group<T> {
    const WIDTH: usize = 5;
    fn code() -> u8 { 2 }
}
type Selected = [u8; <Group<u8> as Rank<u16, u16>>::WIDTH];
fn main() {
    let value: Selected = [17; 5];
    assert_eq!(value, [17; 5]);
    assert_eq!(<Group<u8> as Rank<u16, u16>>::code(), 2);
    assert_eq!(<Group<u8> as Rank<u16, u32>>::WIDTH, 3);
    assert_eq!(<Group<u8> as Rank<u16, u32>>::code(), 1);
}
