//@ compile-variant: -O
#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
trait Shape { type Source; type Target; }
struct Bytes;
impl Shape for Bytes {
    type Source = *const [u8; 3];
    type Target = *const [u8];
}
const fn widen<T: Shape>(pointer: T::Source) -> T::Target
where T::Source: CoerceUnsized<T::Target> { pointer }
static DATA: [u8; 3] = [17, 23, 29];
const POINTER: *const [u8] = widen::<Bytes>(&DATA);
fn main() { assert_eq!(unsafe { &*POINTER }, &[17, 23, 29]); }
