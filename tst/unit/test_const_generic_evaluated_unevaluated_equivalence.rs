#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn min(left: usize, right: usize) -> usize {
    if left < right { left } else { right }
}

trait HasSize<const N: usize> {}

struct Pair<const LEFT: usize, const RIGHT: usize>;

impl<const LEFT: usize, const RIGHT: usize> HasSize<{ min(LEFT, RIGHT) }>
    for Pair<LEFT, RIGHT>
{
}

fn require_one<T: HasSize<1>>() {}

fn main() {
    require_one::<Pair<1, 2>>();
}
