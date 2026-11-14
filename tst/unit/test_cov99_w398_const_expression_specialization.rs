//@ compile-only
#![feature(specialization, generic_const_exprs)]
#![allow(incomplete_features)]

struct Shape<const K: usize>;
trait Same<const N: usize> {}

impl<const N: usize> Same<N> for Shape<N> {}
impl<const K: usize> Same<{ K + 0 }> for Shape<K>
where
    [(); K + 0]:,
{}

fn main() {}
