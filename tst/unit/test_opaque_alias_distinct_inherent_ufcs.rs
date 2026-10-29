//@ compile-fail: Failed to find
#![feature(type_alias_impl_trait)]
use std::marker::PhantomData;
type Hidden<T: Copy> = impl Copy;
#[define_opaque(Hidden)]
fn hidden<T: Copy>(value: T) -> Hidden<T> { value }
struct Pair<T, U>(PhantomData<(T, U)>);
impl<T> Pair<T, T> { fn code() -> u8 { 7 } }
fn wrong() -> u8 { Pair::<Hidden<u8>, Hidden<u16>>::code() }
fn main() {}
