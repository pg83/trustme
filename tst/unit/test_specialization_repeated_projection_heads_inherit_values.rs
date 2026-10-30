//@ compile-flags: -Znext-solver=globally
#![feature(specialization)]
#![allow(incomplete_features)]
trait Origin { type Item; }
trait Mark {}
struct Selected;
impl Origin for Selected { type Item = u8; }
impl Mark for Selected {}
struct Plain;
impl Origin for Plain { type Item = u32; }
struct Wrap<T>(T);
trait Rank<A,B> { type Output; fn value() -> Self::Output; fn code() -> u8; }
impl<T: Origin+Mark> Rank<T::Item,T::Item> for Wrap<T> { fn code() -> u8 { 3 } }
impl<T,U> Rank<U,U> for Wrap<T> { default type Output = u32; default fn value() -> Self::Output { loop {} } default fn code() -> u8 { 1 } }
impl<T: Origin> Rank<T::Item,T::Item> for Wrap<T> { type Output = u16; fn value() -> u16 { 41 } default fn code() -> u8 { 2 } }
fn need<T: Origin>() -> u16 where Wrap<T>: Rank<T::Item,T::Item,Output=u16> { <Wrap<T> as Rank<T::Item,T::Item>>::value() }
fn main() {
 assert_eq!(need::<Selected>(), 41);
 assert_eq!(need::<Plain>(), 41);
 assert_eq!(<Wrap<Selected> as Rank<u8,u8>>::code(), 3);
 assert_eq!(<Wrap<Plain> as Rank<u32,u32>>::code(), 2);
 assert_eq!(<Wrap<()> as Rank<u64,u64>>::code(), 1);
}
