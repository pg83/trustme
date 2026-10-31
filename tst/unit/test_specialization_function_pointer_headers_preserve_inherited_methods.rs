#![feature(specialization)]
#![allow(incomplete_features)]
trait Rank { fn code() -> u8; fn inherited() -> u8; }
impl Rank for fn(u16) -> u32 { fn code() -> u8 { 3 } }
impl<T, U> Rank for fn(T) -> U {
    default fn code() -> u8 { 1 }
    default fn inherited() -> u8 { 13 }
}
impl<U> Rank for fn(u16) -> U { default fn code() -> u8 { 2 } }
fn main() {
    assert_eq!(<fn(u16) -> u32 as Rank>::code(), 3);
    assert_eq!(<fn(u16) -> u32 as Rank>::inherited(), 13);
}
