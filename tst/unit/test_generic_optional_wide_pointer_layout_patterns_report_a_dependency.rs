//@ compile-fail: generic
use std::marker::PhantomData;
struct Layout<T: ?Sized>(PhantomData<T>);
impl<T: ?Sized> Layout<T> {
    const BYTES: usize = {
        let value: Option<*const T> = None;
        std::mem::size_of_val(&value)
    };
}
fn matches<T: ?Sized>(value: usize) -> bool {
    match value { Layout::<T>::BYTES => true, _ => false }
}
fn main() { let _ = matches::<str>(std::mem::size_of::<Option<*const str>>()); }
