//@ compile-variant: -O
#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
trait Source { type Pointer: CoerceUnsized<*const [u16]>; }
struct Words;
impl Source for Words { type Pointer = *const [u16; 3]; }
const fn widen<P: CoerceUnsized<*const [u16]>>(pointer: P) -> *const [u16] { pointer }
const fn projected<T: Source>(pointer: T::Pointer) -> *const [u16] { pointer }
static DATA: [u16; 3] = [17, 23, 29];
const FIRST: *const [u16] = widen(&DATA as *const [u16; 3]);
const SECOND: *const [u16] = projected::<Words>(&DATA);
fn main() {
    assert_eq!(unsafe { &*FIRST }, &[17, 23, 29]);
    assert_eq!(unsafe { &*SECOND }, &[17, 23, 29]);
}
