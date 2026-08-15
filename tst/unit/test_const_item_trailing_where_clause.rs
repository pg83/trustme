#![feature(generic_const_items)]
#![allow(incomplete_features)]

trait Trait {
    const VALUE: usize;
}

const VALUE: usize = <&'static ()>::VALUE
where
    for<'a> &'a (): Trait;

fn main() {}
