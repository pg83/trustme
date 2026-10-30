#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Width { const N: usize; }
struct Measure<T>(PhantomData<T>);
impl<T> Width for Measure<T> { const N: usize = std::mem::size_of::<T>() + 2; }
trait Mark<const N: usize> {}
impl Mark<{Self::N}> for Measure<u8> {}
impl Mark<{Self::N}> for Measure<u16> {}
struct Runner;
impl Runner {
    const fn length<T: Width>() -> usize where T: Mark<{T::N}> { T::N }
}
trait Length { const TAG: usize; }
impl Length for [u8; 3] { const TAG: usize = 17; }
impl Length for [u8; 4] { const TAG: usize = 19; }
type First = [u8; <[u8; Runner::length::<Measure<u8>>() ]>::TAG];
type Second = [u8; <[u8; Runner::length::<Measure<u16>>() ]>::TAG];
fn main() {
    let first: First = [7; 17];
    let second: Second = [11; 19];
    assert_eq!(first, [7; 17]);
    assert_eq!(second, [11; 19]);
}
