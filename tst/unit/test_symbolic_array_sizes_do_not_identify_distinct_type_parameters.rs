//@ compile-fail: Type mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
struct Size<T>(PhantomData<T>);
impl<T> Size<T> { const WIDTH: usize = std::mem::size_of::<T>(); }
fn change<T, U>(value: [u8; Size::<T>::WIDTH]) -> [u8; Size::<U>::WIDTH] { value }
fn main() {}
