//@ crate-type: lib
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use core::mem::size_of;

trait Trait<T> {
    fn first(self);
    fn second(self);
}

struct Value;

impl<T> Trait<T> for Value
where
    [(); size_of::<T>()]: Sized,
{
    fn first(self) {
        self.second();
    }

    fn second(self) {}
}

fn main() {}
