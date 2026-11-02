//@ compile-variant: -O
#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
#[repr(C)]
struct Handle<T: ?Sized> { tag: u64, pointer: *const T }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Handle<U>> for Handle<T> {}
const fn widen<P: CoerceUnsized<Q>, Q>(pointer: P) -> Q { pointer }
static VALUES: [u16; 3] = [17, 23, 29];
const HANDLE: Handle<[u16]> = widen(Handle { tag: 0x1234_5678_9abc_def0, pointer: &VALUES });
fn main() {
    assert_eq!(HANDLE.tag, 0x1234_5678_9abc_def0);
    assert_eq!(unsafe { &*HANDLE.pointer }, &[17, 23, 29]);
}
