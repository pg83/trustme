#![feature(specialization)]
#![allow(incomplete_features)]
trait Value { type Item; }
trait Rank<T> { fn code() -> u8; fn inherited() -> u8; }
impl<T> Rank<T> for dyn Value<Item = T> {
    default fn code() -> u8 { 1 }
    default fn inherited() -> u8 { 11 }
}
impl Rank<u16> for dyn Value<Item = u16> { fn code() -> u8 { 2 } }
fn main() {
    assert_eq!(<dyn Value<Item = u16> as Rank<u16>>::code(), 2);
    assert_eq!(<dyn Value<Item = u16> as Rank<u16>>::inherited(), 11);
}
