//@ compile-variant: -O
#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
#[repr(C, packed)]
struct Handle<T: ?Sized> { tag: u8, pointer: *const T }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Handle<U>> for Handle<T> {}
const fn widen<P: CoerceUnsized<Q>, Q>(pointer: P) -> Q { pointer }
static DATA: [u16; 3] = [17, 23, 29];
const HANDLE: Handle<[u16]> = widen(Handle { tag: 31, pointer: &DATA });
fn main() {
    let pointer = HANDLE.pointer;
    assert_eq!(HANDLE.tag, 31);
    assert_eq!(unsafe { &*pointer }, &[17, 23, 29]);
}
