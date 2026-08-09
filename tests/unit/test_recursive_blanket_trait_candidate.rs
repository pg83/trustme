// A generic body may rely on its own where-clause while a structurally
// applicable blanket impl is also being considered. Candidate exploration
// must recognise the repeated goal modulo fresh impl placeholders: it is an
// ambiguous candidate, not unbounded recursion.

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

trait Rng {
    fn random<T>(&mut self) -> T
    where
        Standard: Distribution<T>,
    {
        Standard.sample(self)
    }

    fn sample_iter<T, D>(self, distr: D) -> Iter<D, Self, T>
    where
        D: Distribution<T>,
        Self: Sized,
    {
        distr.sample_iter(self)
    }

    fn random_iter<T>(self) -> Iter<Standard, Self, T>
    where
        Self: Sized,
        Standard: Distribution<T>,
    {
        Standard.sample_iter(self)
    }
}

struct Standard;

impl Distribution<u8> for Standard {
    fn sample<R: Rng + ?Sized>(&self, _: &mut R) -> u8 {
        0
    }
}

impl<T, D: Distribution<T> + ?Sized> Distribution<T> for &D {
    fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> T {
        (*self).sample(rng)
    }
}

impl<A> Distribution<(A,)> for Standard
where
    Standard: Distribution<A>,
{
    fn sample<R: Rng + ?Sized>(&self, rng: &mut R) -> (A,) {
        (rng.random::<A>(),)
    }
}

fn main() {}
