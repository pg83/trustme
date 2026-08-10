//@ check-pass
//@ compile-flags: -Znext-solver

#![feature(rustc_attrs)]

#[rustc_coinductive]
trait Cycle<T> {}

impl<T, U> Cycle<U> for T
where
    T: Cycle<U>,
{
}

fn requires_cycle<T: Cycle<u8>>() {}

fn main() {
    requires_cycle::<()>();
}
