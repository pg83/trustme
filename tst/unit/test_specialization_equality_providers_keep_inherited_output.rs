//@ compile-flags: -Znext-solver=globally
#![feature(specialization)]
#![allow(incomplete_features)]
trait Source { type Kind; type Extra; }
trait Forward { type Output; fn code() -> u8; fn width() -> usize { std::mem::size_of::<Self::Output>() } }
trait Reverse { type Output; fn code() -> u8; fn width() -> usize { std::mem::size_of::<Self::Output>() } }
impl<T: Source<Kind = u16, Extra = u8> + Copy> Forward for T { fn code() -> u8 { 40 } }
impl<T: Source> Forward for T { default type Output = u8; default fn code() -> u8 { 10 } }
impl<T: Source<Kind = u16>> Forward for T { default type Output = u16; default fn code() -> u8 { 20 } }
impl<T: Source<Kind = u16, Extra = u8>> Forward for T { type Output = [u8; 3]; default fn code() -> u8 { 30 } }
impl<T: Source<Kind = u16, Extra = u8> + Copy> Reverse for T { fn code() -> u8 { 40 } }
impl<T: Source<Kind = u16, Extra = u8>> Reverse for T { type Output = [u8; 3]; default fn code() -> u8 { 30 } }
impl<T: Source<Kind = u16>> Reverse for T { default type Output = u16; default fn code() -> u8 { 20 } }
impl<T: Source> Reverse for T { default type Output = u8; default fn code() -> u8 { 10 } }
struct Plain;
struct Kind;
struct KindExtra;
#[derive(Clone, Copy)] struct CopyKindExtra;
impl Source for Plain { type Kind = u8; type Extra = usize; }
impl Source for Kind { type Kind = u16; type Extra = usize; }
impl Source for KindExtra { type Kind = u16; type Extra = u8; }
impl Source for CopyKindExtra { type Kind = u16; type Extra = u8; }
fn check<T: Forward + Reverse>(code: u8, width: usize) {
    assert_eq!((<T as Forward>::code(), <T as Reverse>::code()), (code, code));
    assert_eq!((<T as Forward>::width(), <T as Reverse>::width()), (width, width));
}
fn main() {
    check::<Plain>(10, 1);
    check::<Kind>(20, 2);
    check::<KindExtra>(30, 3);
    check::<CopyKindExtra>(40, 3);
}
