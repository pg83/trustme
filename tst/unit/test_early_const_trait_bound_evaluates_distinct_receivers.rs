#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Width { const N: usize; }
impl Width for u8 { const N: usize = 3; }
impl Width for u16 { const N: usize = 3; }
trait Mark<const N: usize> {}
impl Mark<{Self::N}> for u8 {}
struct Runner;
impl Runner {
    const fn length<T: Width>() -> usize
    where u8: Mark<{T::N}>,
    { 3 }
}
trait Length { const TAG: usize; }
impl Length for [u8; 3] { const TAG: usize = 17; }
type Selected = [u8; <[u8; Runner::length::<u16>()]>::TAG];
fn main() { let value: Selected = [19; 17]; assert_eq!(value, [19; 17]); }
