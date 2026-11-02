//@ compile-variant: -O
#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
#[repr(C)]
struct Handle<T: ?Sized> { pointer: *const T, tag: [u16; 2] }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Handle<U>> for Handle<T> {}
#[repr(C)]
struct Outer<P> { before: u64, inner: P, after: u64 }
impl<P: CoerceUnsized<Q>, Q> CoerceUnsized<Outer<Q>> for Outer<P> {}
const fn widen<P: CoerceUnsized<Q>, Q>(pointer: P) -> Q { pointer }
static VALUES: [u8; 3] = [17, 23, 29];
const HANDLE: Outer<Handle<[u8]>> = widen(Outer {
    before: 31,
    inner: Handle { pointer: &VALUES, tag: [37, 41] },
    after: 43,
});
fn main() {
    assert_eq!(HANDLE.before, 31);
    assert_eq!(HANDLE.inner.tag, [37, 41]);
    assert_eq!(unsafe { &*HANDLE.inner.pointer }, &[17, 23, 29]);
    assert_eq!(HANDLE.after, 43);
}
