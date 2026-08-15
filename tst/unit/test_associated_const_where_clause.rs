#![feature(generic_const_items)]
#![allow(incomplete_features, dead_code)]

trait Trait {
    const VALUE: usize
    where
        Self: Sized;
}

fn accepts(_: &dyn Trait) {}

fn main() {}
