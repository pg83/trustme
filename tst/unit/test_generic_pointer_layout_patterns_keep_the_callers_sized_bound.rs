//@ compile-variant: -O
use std::marker::PhantomData;

struct Layout<T>(PhantomData<T>);
impl<T> Layout<T> {
    const POINTER_BYTES: usize = std::mem::size_of::<*const T>();
}
fn matches<T>(value: usize) -> bool {
    match value { Layout::<T>::POINTER_BYTES => true, _ => false }
}
fn main() {
    let bytes = std::mem::size_of::<usize>();
    assert!(matches::<u8>(bytes));
    assert!(matches::<String>(bytes));
    assert!(!matches::<u8>(bytes + 1));
}
