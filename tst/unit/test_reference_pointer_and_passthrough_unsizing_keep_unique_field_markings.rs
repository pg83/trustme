#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
struct Reference<'a, T: ?Sized>(&'a T);
impl<'a, T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Reference<'a, U>> for Reference<'a, T> {}
struct Outer<T>(T);
impl<T: CoerceUnsized<U>, U> CoerceUnsized<Outer<U>> for Outer<T> {}
fn main() {
    let bytes = [11u8, 13, 17];
    let source: Outer<Reference<'_, [u8; 3]>> = Outer(Reference(&bytes));
    let target: Outer<Reference<'_, [u8]>> = source;
    assert_eq!(target.0.0, &[11, 13, 17]);
}
