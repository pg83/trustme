//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(sized_hierarchy)]

use std::marker::MetaSized;

trait Project<const N: usize> {
    type Out: ?Sized;
}

struct Known;

impl<const N: usize> Project<N> for Known {
    type Out = u8;
}

struct Tail<T: Project<N>, const N: usize>(<T as Project<N>>::Out);

trait Chosen {}
impl Chosen for Tail<Known, 5> {}

fn accept<T: ?Sized + MetaSized + Chosen>() {}

pub fn probe() {
    accept::<Tail<Known, _>>();
}
