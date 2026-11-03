#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
#[repr(C)] struct Tail<T: ?Sized> { tag: u16, values: T }
struct Pointer<T: ?Sized>(*const T);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Pointer<U>> for Pointer<T> {}
fn main() {
    let data = Tail { tag: 241, values: [2u8, 3, 5, 7] };
    let source: Pointer<Tail<[u8; 4]>> = Pointer(&data);
    let target: Pointer<Tail<[u8]>> = source;
    assert_eq!(unsafe { (*target.0).tag }, 241);
    assert_eq!(unsafe { &(*target.0).values }, &[2, 3, 5, 7]);
}
