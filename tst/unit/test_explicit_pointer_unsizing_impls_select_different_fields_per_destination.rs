#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
trait Read { fn read(&self) -> u16; }
impl Read for u16 { fn read(&self) -> u16 { *self } }
struct Pair<A: ?Sized, B: ?Sized>(*const A, *const B);
impl CoerceUnsized<Pair<[u8], u16>> for Pair<[u8; 2], u16> {}
impl CoerceUnsized<Pair<[u8; 2], dyn Read>> for Pair<[u8; 2], u16> {}
fn main() {
    let bytes = [11u8, 13];
    let number = 197u16;
    let first: Pair<[u8], u16> = Pair(&bytes as *const [u8; 2], &number as *const u16);
    assert_eq!(unsafe { &*first.0 }, &[11, 13]);
    assert_eq!(unsafe { *first.1 }, 197);
    let second: Pair<[u8; 2], dyn Read> = Pair(&bytes as *const [u8; 2], &number as *const u16);
    assert_eq!(unsafe { (*second.0)[1] }, 13);
    assert_eq!(unsafe { &*second.1 }.read(), 197);
}
