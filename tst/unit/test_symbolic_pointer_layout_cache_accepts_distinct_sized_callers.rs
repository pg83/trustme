//@ compile-variant: -O
use std::marker::PhantomData;
struct Layout<T: ?Sized>(PhantomData<T>);
impl<T: ?Sized> Layout<T> {
    const BYTES: usize = std::mem::size_of::<*const T>();
}
fn a_sized<T>(value: usize) -> bool {
    match value { Layout::<T>::BYTES => true, _ => false }
}
fn b_sized<T: Clone>(value: usize) -> bool {
    match value { Layout::<T>::BYTES => true, _ => false }
}
fn main() {
    let bytes = std::mem::size_of::<usize>();
    assert!(a_sized::<u8>(bytes));
    assert!(b_sized::<String>(bytes));
    assert!(!a_sized::<u8>(bytes + 1));
    assert!(!b_sized::<String>(bytes + 1));
}
