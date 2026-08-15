#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Outer {}

impl<const N: usize> Outer for [(); N]
where
    (): Predicate<{ N == 0 }>,
{
}

trait Predicate<const VALUE: bool> {}

impl Predicate<true> for () {}
impl Predicate<false> for () {}

fn require_outer<T: Outer>(_: T) {}

fn main() {
    require_outer([]);
    require_outer([()]);
}
