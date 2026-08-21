//@ compile-fail: Trait

#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn min(left: usize, right: usize) -> usize {
    if left < right { left } else { right }
}

trait Trait<T> {
    fn call() {}
}

struct Value<const LEFT: usize, const RIGHT: usize>;

impl<const LEFT: usize, const RIGHT: usize> Trait<[u8; min(LEFT, RIGHT)]>
    for Value<LEFT, RIGHT>
{}

fn main() {
    <Value<2, 3> as Trait<[u8; 1]>>::call();
}
