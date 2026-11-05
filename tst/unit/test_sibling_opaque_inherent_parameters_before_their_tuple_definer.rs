//@ compile-fail: Failed to find
#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;
type Tokens = (impl Copy, impl Clone);
trait Parts { type Front; type Back; }
impl<A, B> Parts for (A, B) { type Front = A; type Back = B; }
type Front = <Tokens as Parts>::Front;
type Back = <Tokens as Parts>::Back;

struct EqualPair<A, B>(PhantomData<(A, B)>);
impl<T> EqualPair<T, T> {
    fn code() -> u8 { 31 }
}

fn a_check() -> u8 { EqualPair::<Front, Back>::code() }

#[define_opaque(Tokens)]
fn z_tokens() -> Tokens { (37u8, 41u8) }

fn main() { let _ = a_check(); }
