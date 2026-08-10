// An unknown trait argument must not make the legacy solver recursively
// enumerate every tuple-shaped blanket candidate.  rustc treats a repeated
// trait with fresh input types as ambiguous before exploring those candidates.

use core::marker::PhantomData;

struct Iter<D, R, T> {
    distribution: D,
    rng: R,
    marker: PhantomData<T>,
}

trait Distribution<T> {
    fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> T;

    fn sample_iter<R>(self, rng: R) -> Iter<Self, R, T>
    where
        R: Rng,
        Self: Sized,
    {
        Iter {
            distribution: self,
            rng,
            marker: PhantomData,
        }
    }
}

impl<T, D: Distribution<T> + ?Sized> Distribution<T> for &D {
    fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> T {
        (*self).sample(rng)
    }
}

trait Rng {
    fn random<T>(&mut self) -> T
    where
        Standard: Distribution<T>,
    {
        Standard.sample(self)
    }

    fn random_iter<T>(self) -> Iter<Standard, Self, T>
    where
        Self: Sized,
        Standard: Distribution<T>,
    {
        Standard.sample_iter(self)
    }
}

impl<R: ?Sized> Rng for R {}

struct Standard;

impl Distribution<u8> for Standard {
    fn sample<R: Rng + ?Sized>(&self, _: &mut R) -> u8 {
        0
    }
}

macro_rules! tuple_impl {
    ($($ty:ident)*) => {
        impl<$($ty,)*> Distribution<($($ty,)*)> for Standard
        where
            $(Standard: Distribution<$ty>,)*
        {
            fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> ($($ty,)*) {
                let value = ($(rng.random::<$ty>(),)*);
                let _ = rng;
                value
            }
        }
    };
}

macro_rules! tuple_impls {
    ($($ty:ident)*) => {
        tuple_impls!([] $($ty)*);
    };
    ([$($prefix:ident)*] $head:ident $($tail:ident)*) => {
        tuple_impl!($($prefix)*);
        tuple_impls!([$($prefix)* $head] $($tail)*);
    };
    ([$($prefix:ident)*]) => {
        tuple_impl!($($prefix)*);
    };
}

tuple_impls!(A B C D E F G H);

struct ThreadRng;

fn rng() -> ThreadRng {
    ThreadRng
}

fn random_iter<T>() -> Iter<Standard, ThreadRng, T>
where
    Standard: Distribution<T>,
{
    rng().random_iter()
}

fn main() {}
