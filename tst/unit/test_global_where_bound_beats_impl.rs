// A where-clause names the associated type, and the caller is compiled under
// what the clause says even when an impl says otherwise: the clause is what the
// body was checked against. `B: A<X = i32>` holds of no real B, which is the
// point of the feature - the body is still checked as if it did. Reading the
// impl instead made `B::get_x()` a u8 where the signature says i32.

#![feature(trivial_bounds)]
#![allow(unused)]

struct B;

trait A {
    type X;

    fn get_x() -> Self::X;
}

impl A for B {
    type X = u8;

    fn get_x() -> u8 {
        0
    }
}

fn inconsistent_bound() -> i32
where
    B: A<X = i32>,
{
    B::get_x()
}

fn main() {}
