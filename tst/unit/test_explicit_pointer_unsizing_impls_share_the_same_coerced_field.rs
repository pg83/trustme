#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
struct Pointer<T: ?Sized>(*const T);
impl CoerceUnsized<Pointer<[u16]>> for Pointer<[u16; 2]> {}
impl CoerceUnsized<Pointer<[u16]>> for Pointer<[u16; 3]> {}
fn main() {
    let first = [173u16, 179];
    let second = [181u16, 191, 193];
    let first_slice: Pointer<[u16]> = Pointer(&first as *const [u16; 2]);
    let second_slice: Pointer<[u16]> = Pointer(&second as *const [u16; 3]);
    assert_eq!(unsafe { &*first_slice.0 }, &[173, 179]);
    assert_eq!(unsafe { &*second_slice.0 }, &[181, 191, 193]);
}
