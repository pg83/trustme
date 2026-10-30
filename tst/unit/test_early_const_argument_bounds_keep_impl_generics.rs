#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Width { const N: usize; const OTHER: usize; }
impl Width for u8 { const N: usize = 3; const OTHER: usize = 5; }
impl Width for u16 { const N: usize = 3; const OTHER: usize = 5; }
struct Param<const N: usize>;
trait Family { type Input; }
impl Family for u8 { type Input = Param<{Self::N}>; }
struct Runner<T>(std::marker::PhantomData<T>);
impl<T: Width> Runner<T> {
    const fn length() -> usize
    where u8: Family<Input = Param<{T::N}>>,
    { 3 }
}
trait Length { const TAG: usize; }
impl Length for [u8; 3] { const TAG: usize = 17; }
type Selected = [u8; <[u8; Runner::<u16>::length()]>::TAG];
fn main() { let value: Selected = [19; 17]; assert_eq!(value, [19; 17]); }
