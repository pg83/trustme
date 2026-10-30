#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
trait Width { const N: usize; }
impl Width for u8 { const N: usize = 3; }
struct Left<T>(PhantomData<T>);
struct Right<T>(PhantomData<T>);
trait First<const N: usize> {}
trait Second<const N: usize> {}
impl<T: Width> First<{T::N}> for Left<T> where [(); T::N]: {}
impl<T: Width> Second<{T::N}> for Right<T> where [(); T::N]: {}
const fn join<A, B, const N: usize>(_: Left<A>, _: Right<B>) -> usize
where Left<A>: First<N>, Right<B>: Second<N>,
{ 3 }
struct Runner;
impl Runner {
    const fn length<T: Width>() -> usize where [(); T::N]: {
        join(Left::<T>(PhantomData), Right::<_>(PhantomData))
    }
}
trait Length { const TAG: usize; }
impl Length for [u8; 3] { const TAG: usize = 17; }
type Selected = [u8; <[u8; Runner::length::<u8>()]>::TAG];
fn main() { let value: Selected = [19; 17]; assert_eq!(value, [19; 17]); }
