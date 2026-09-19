/* A method call on a receiver that is no `DerefMut` used to hang the compiler.

   `rand_core` writes the blanket

       impl<R: DerefMut> TryRng for R where R::Target: TryRng {
           type Error = <R::Target as TryRng>::Error;
       }

   and `Rng` is `TryRng<Error = Infallible>`, so probing `a.pick()` on an
   `&Alphabetic` asks whether `&Alphabetic: Rng`, which asks
   `Alphabetic: TryRng<Error = ..>`, which offers that blanket impl.  Its two
   bounds are `R: DerefMut` and `R::Target: TryRng`; the first has no solution
   for a plain struct, and upstream never looks at the second - it takes a
   candidate's nested goals in the order the impl writes them and returns at
   the first that has no solution (`evaluate_predicates_recursively`,
   rustc_trait_selection/src/traits/select/mod.rs; the `?` on `evaluate_goal_raw`
   in `evaluate_added_goals_step`, rustc_next_trait_solver/src/solve/eval_ctxt).
   Preparing the second bound first never comes back: `<Alphabetic as Deref>::Target`
   is rigid, the same impl reads it as its own `R`, and the bound to prepare is one
   `Deref` deeper every round.

   With the candidate gone, `Distribution::pick` is the only method left, as it
   is for upstream. */

use std::ops::DerefMut;

pub enum Never {}

pub trait TryRng {
    type Error;
}

impl<R: DerefMut> TryRng for R
where
    R::Target: TryRng,
{
    type Error = <R::Target as TryRng>::Error;
}

pub trait Rng: TryRng<Error = Never> {}

impl<R> Rng for R where R: TryRng<Error = Never> + ?Sized {}

pub trait RngExt: Rng {
    fn pick(self) -> u8
    where
        Self: Sized,
    {
        1
    }
}

impl<R: Rng + ?Sized> RngExt for R {}

pub trait Distribution<T> {
    fn pick(self) -> u8
    where
        Self: Sized,
    {
        2
    }
}

pub struct Alphabetic;

impl Distribution<u8> for Alphabetic {}

impl<T, D: Distribution<T> + ?Sized> Distribution<T> for &D {}

fn sample(alphabetic: &Alphabetic) -> u8 {
    alphabetic.pick()
}

fn main() {
    assert_eq!(sample(&Alphabetic), 2);
}
