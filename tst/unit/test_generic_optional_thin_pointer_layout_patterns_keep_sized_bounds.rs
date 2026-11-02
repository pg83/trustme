//@ compile-variant: -O
use std::marker::PhantomData;
struct Layout<T>(PhantomData<T>);
impl<T> Layout<T> {
    const BYTES: usize = {
        let value: Option<*const T> = None;
        std::mem::size_of_val(&value)
    };
}
fn matches<T>(value: usize) -> bool {
    match value { Layout::<T>::BYTES => true, _ => false }
}
fn main() {
    let bytes = std::mem::size_of::<Option<*const u8>>();
    assert!(matches::<u8>(bytes));
    assert!(matches::<String>(bytes));
    assert!(!matches::<u8>(bytes + 1));
}
