#![feature(specialization, never_type)]

trait Pick { const TAG: u16; }
impl<T> Pick for fn(T) -> ! { default const TAG: u16 = 3; }
impl Pick for fn(u8) -> ! { const TAG: u16 = 11; }
struct Pair<A, B>(A, B);
impl<T> Pick for Pair<T, T> { const TAG: u16 = 17; }
trait Tag { fn tag(&self) -> u16; }
impl<T> Tag for Pair<T, T> { fn tag(&self) -> u16 { 23 } }
fn value() -> u16 { 5 }

fn main() {
    assert_eq!(<fn(u8) -> ! as Pick>::TAG, 11);
    assert_eq!(<fn(u16) -> ! as Pick>::TAG, 3);
    assert_eq!(<fn(bool) -> ! as Pick>::TAG, 3);
    assert_eq!(<Pair<fn(u8) -> !, fn(u8) -> !> as Pick>::TAG, 17);
    assert_eq!(<Pair<fn() -> !, fn() -> !> as Pick>::TAG, 17);
    assert_eq!(Pair(value, value).tag(), 23);
    let callback = || 7u16;
    assert_eq!(Pair(callback, callback).tag(), 23);
}
