// A method whose generic parameter is fixed only by the closure argument's
// return type cannot be resolved on the first look: the return type is a
// projection through that parameter. Each inconclusive lookup instantiates the
// signature afresh, and if it exports the obligations it collected, those name
// variables belonging to that attempt alone. Nothing can ever bind them, so
// typecheck ends holding rules it cannot discharge and reports that annotations
// are needed for a call that is fully determined.

#![feature(try_trait_v2, try_trait_v2_residual)]

use std::ops::{Residual, Try};

trait Reduce: Sized {
    fn reduce<T, R>(self, _seed: T, _f: impl FnOnce(T) -> R) -> <R::Residual as Residual<Option<T>>>::TryType
    where
        R: Try<Output = T>,
        R::Residual: Residual<Option<T>>,
    {
        Try::from_output(None)
    }
}

struct V;

impl Reduce for V {}

fn main() {
    let x = V.reduce(1usize, |a| a.checked_add(1));
    assert_eq!(x, Some(None));
}
