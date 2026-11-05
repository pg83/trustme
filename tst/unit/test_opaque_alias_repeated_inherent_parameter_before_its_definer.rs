//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;
type Token = impl Copy;

struct EqualPair<A, B>(PhantomData<(A, B)>);
impl<T> EqualPair<T, T> {
    fn code() -> u8 { 13 }
}

fn a_check() -> u8 { EqualPair::<Token, Token>::code() }

#[define_opaque(Token)]
fn z_token() -> Token { 17u16 }

fn main() {
    assert_eq!(a_check(), 13);
    assert_eq!(std::mem::size_of_val(&z_token()), 2);
}
