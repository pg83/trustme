//@ compile-fail: Failed to find
#![feature(type_alias_impl_trait)]
use std::marker::PhantomData;
type A = impl Copy;
type B = impl Copy;
#[define_opaque(A, B)]
fn values() -> (A, B) { (3u8, 5u8) }
struct Pair<T, U>(PhantomData<(T, U)>);
impl<T> Pair<T, T> { fn code() -> u8 { 7 } }
fn wrong() -> u8 { Pair::<A, B>::code() }
fn main() {}
