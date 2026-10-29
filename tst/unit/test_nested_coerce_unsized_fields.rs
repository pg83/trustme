#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
struct Inner<T: ?Sized>(Box<T>);
struct Outer<T: ?Sized> { inner: Inner<T> }
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Inner<U>> for Inner<T> {}
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Outer<U>> for Outer<T> {}
trait Read { fn read(&self) -> u32; }
impl Read for u32 { fn read(&self) -> u32 { *self } }
fn main() {
    let slice: Outer<[u16]> = Outer { inner: Inner(Box::new([3, 5, 7])) };
    assert_eq!(&*slice.inner.0, &[3, 5, 7]);
    let object: Outer<dyn Read> = Outer { inner: Inner(Box::new(23u32)) };
    assert_eq!(object.inner.0.read(), 23);
}
