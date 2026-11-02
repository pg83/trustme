//@ compile-fail: generic
use std::marker::PhantomData;
struct Layout<T: ?Sized>(PhantomData<T>);
impl<T: ?Sized> Layout<T> {
    const BYTES: usize = std::mem::size_of::<*const T>();
}
fn a_sized<T>(value: usize) -> bool {
    match value { Layout::<T>::BYTES => true, _ => false }
}
fn z_unsized<T: ?Sized>(value: usize) -> bool {
    match value { Layout::<T>::BYTES => true, _ => false }
}
fn main() {
    assert!(a_sized::<u8>(std::mem::size_of::<usize>()));
    let _ = z_unsized::<str>(2 * std::mem::size_of::<usize>());
}
