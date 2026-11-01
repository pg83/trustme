//@ compile-fail: generic
use std::marker::PhantomData;
struct Layout<T>(PhantomData<T>);
impl<T> Layout<T> { const ALIGNMENT: usize = std::mem::align_of::<T>(); }
fn matches<T>(value: usize) -> bool {
    match value { Layout::<T>::ALIGNMENT => true, _ => false }
}
fn main() { let _ = matches::<u32>(4); }
