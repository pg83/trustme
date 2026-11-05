//@ compile-fail: Failed to find
#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;
type Front = impl Copy;
type Back = impl Copy;

struct EqualPair<A, B>(PhantomData<(A, B)>);
impl<T> EqualPair<T, T> {
    fn code() -> u8 { 19 }
}

fn a_check() -> u8 { EqualPair::<Front, Back>::code() }

#[define_opaque(Front, Back)]
fn z_values() -> (Front, Back) { (23u8, 29u8) }

fn main() { let _ = a_check(); }
