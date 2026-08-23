// Regression: a concrete associated const on a generic type must be evaluated
// before MIR is saved for the enclosing anonymous constant.
//@ crate-type: lib

use std::marker::PhantomData;

struct Lookup<T>(PhantomData<T>);

impl<T> Lookup<T> {
    const MIN_SIZE: usize = 6;
}

const fn long_enough(size: usize) -> bool {
    size <= 6
}

const _: () = assert!(long_enough(Lookup::<()>::MIN_SIZE));
