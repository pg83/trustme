//@ compile-fail: generic
#![feature(core_intrinsics)]
#![allow(internal_features)]
use std::marker::PhantomData;
struct Layout<T>(PhantomData<T>);
impl<T> Layout<T> { const ALIGNMENT: usize = std::intrinsics::align_of::<T>(); }
fn matches<T>(value: usize) -> bool {
    match value { Layout::<T>::ALIGNMENT => true, _ => false }
}
fn main() { let _ = matches::<u32>(4); }
