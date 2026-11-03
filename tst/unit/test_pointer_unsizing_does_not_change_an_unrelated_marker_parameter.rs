//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::marker::{PhantomData, Unsize};
use std::ops::CoerceUnsized;
struct Tagged<K, T: ?Sized> { marker: PhantomData<K>, pointer: *const T }
impl<K, T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Tagged<K, U>> for Tagged<K, T> {}
fn main() {
    let bytes = [31u8, 37];
    let source: Tagged<u8, [u8; 2]> = Tagged { marker: PhantomData, pointer: &bytes };
    let target: Tagged<u16, [u8]> = source;
    assert_eq!(unsafe { &*target.pointer }.len(), 2);
}
