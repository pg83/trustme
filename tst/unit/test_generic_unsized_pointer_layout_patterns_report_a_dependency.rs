//@ compile-fail: generic
use std::marker::PhantomData;

struct Layout<T: ?Sized>(PhantomData<T>);
impl<T: ?Sized> Layout<T> {
    const POINTER_BYTES: usize = std::mem::size_of::<*const T>();
}
fn matches<T: ?Sized>(value: usize) -> bool {
    match value { Layout::<T>::POINTER_BYTES => true, _ => false }
}
fn main() { let _ = matches::<str>(2 * std::mem::size_of::<usize>()); }
