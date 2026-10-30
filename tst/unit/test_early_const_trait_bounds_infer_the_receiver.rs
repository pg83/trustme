#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Width { const N: usize; }
impl Width for u8 { const N: usize = 3; }
struct Wrap<T>(std::marker::PhantomData<T>);
trait Mark<const N: usize> {}
impl<T: Width> Mark<{T::N}> for Wrap<T> where [(); T::N]: {}
const fn demand<T: Width, U: Width>() -> usize
where Wrap<T>: Mark<{U::N}>,
{ 3 }
struct Runner;
impl Runner {
    const fn length<T: Width>() -> usize where [(); T::N]: {
        demand::<T, _>()
    }
}
trait Length { const TAG: usize; }
impl Length for [u8; 3] { const TAG: usize = 17; }
type Selected = [u8; <[u8; Runner::length::<u8>()]>::TAG];
fn main() { let value: Selected = [19; 17]; assert_eq!(value, [19; 17]); }
