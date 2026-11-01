//@ compile-variant: -O
#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
#[derive(Copy, Clone)]
struct Ptr<T: ?Sized>(*const T);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Ptr<U>> for Ptr<T> {}
trait Read { fn read(&self) -> u32; }
impl Read for u32 { fn read(&self) -> u32 { *self } }
const DATA: [u32; 3] = [7, 13, 23];
const fn widen<T: ?Sized + Unsize<U>, U: ?Sized>(pointer: Ptr<T>) -> Ptr<U> { pointer }
const SLICE: Ptr<[u32]> = widen(Ptr(&DATA));
const OBJECT: Ptr<dyn Read> = widen(Ptr(&DATA[1]));
fn main() {
    unsafe {
        assert_eq!(&*SLICE.0, &DATA);
        assert_eq!((*OBJECT.0).read(), 13);
    }
}
