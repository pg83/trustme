#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Assert<const VALUE: bool>;
trait Bound {}

struct Subject<const N: usize>;
trait Marker {}

impl<const N: usize> Marker for Subject<N>
where
    Assert<{ N > 0 }>: Bound,
    Assert<{ N > 1 }>: Bound,
{
}

fn main() {}
