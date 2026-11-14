//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

trait Provider {
    type Output;
}

fn require_provider<P: Provider>() {}

pub fn probe<F>()
where
    for<T> F: Provider<Output = T>,
{
    require_provider::<F>();
}
