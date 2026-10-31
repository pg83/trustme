#![feature(specialization, type_alias_impl_trait)]
#![allow(incomplete_features)]
type Hidden<const N: usize> = impl Copy;
#[define_opaque(Hidden)]
fn make<const N: usize>() -> Hidden<N> { [0u8; N] }
trait Rank<const N: usize, U> { fn code() -> u8; fn base() -> u8; }
impl<const N: usize, U> Rank<N, U> for Hidden<N> {
    default fn code() -> u8 { 1 }
    default fn base() -> u8 { 7 }
}
impl<U> Rank<4, U> for Hidden<4> { default fn code() -> u8 { 2 } }
impl Rank<4, u8> for Hidden<4> { fn code() -> u8 { 3 } }
fn main() {
    let _ = make::<4>();
    assert_eq!(<Hidden<4> as Rank<4, u8>>::code(), 3);
    assert_eq!(<Hidden<4> as Rank<4, u8>>::base(), 7);
}
