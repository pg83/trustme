//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Base<const N: usize> {
    type Out;
}

trait Child<const N: usize>: Base<{ N + 1 }>
where
    [(); N + 1]:,
{}

trait Host<const N: usize>
where
    [(); N + 1]:,
{
    type Value: Child<N, Out = ()>;
}

impl<S: ?Sized, const N: usize> Base<N> for S {
    type Out = ();
}

fn require_base<const N: usize, S: ?Sized + Base<N>>(_: &S) {}

fn check<const N: usize, H: Host<N>>(value: &H::Value)
where
    [(); N + 1]:,
{
    require_base::<{ N + 1 }, _>(value);
}

fn main() {}
