//@ compile-flags: -Znext-solver=globally
#![feature(specialization)]
#![allow(incomplete_features)]
trait Rank<A> { type Output; fn value() -> Self::Output; fn code() -> u8; }
struct Wrap<T>(T);
impl Rank<u16> for Wrap<Vec<u8>> { fn code() -> u8 { 3 } }
impl<T> Rank<u16> for Wrap<T> { default type Output = u32; default fn value() -> Self::Output { loop {} } default fn code() -> u8 { 1 } }
impl<T> Rank<u16> for Wrap<Vec<T>> { type Output = u16; fn value() -> u16 { 37 } default fn code() -> u8 { 2 } }
fn need<T: Rank<u16,Output=u16>>() -> u16 { T::value() }
fn main() {
 assert_eq!(need::<Wrap<Vec<u8>>>(), 37);
 assert_eq!(need::<Wrap<Vec<u32>>>(), 37);
 assert_eq!(<Wrap<u32> as Rank<u16>>::code(), 1);
 assert_eq!(<Wrap<Vec<u8>> as Rank<u16>>::code(), 3);
 assert_eq!(<Wrap<Vec<u32>> as Rank<u16>>::code(), 2);
}
