//@ compile-variant: -O
#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
#[repr(C)]
struct Handle<T: ?Sized> { tag: u16, pointer: *const T }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Handle<U>> for Handle<T> {}
#[inline(never)]
const fn widen<P: CoerceUnsized<Q>, Q>(pointer: P) -> Q { pointer }
fn main() {
    let values = std::hint::black_box([17u16, 23, 29]);
    let handle: Handle<[u16]> = widen(Handle { tag: 31, pointer: &values });
    assert_eq!(handle.tag, 31);
    assert_eq!(unsafe { &*handle.pointer }, &[17, 23, 29]);
    let pointer: *const [u16] = widen(&values as *const [u16; 3]);
    assert_eq!(unsafe { &*pointer }, &[17, 23, 29]);
}
