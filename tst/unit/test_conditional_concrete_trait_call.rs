//@ crate-type: lib

#![feature(trivial_bounds)]
#![allow(private_bounds, trivial_bounds)]

trait Required {
    fn method(self);
}

pub fn conditional<T>()
where
    &'static str: Required,
{
    "value".method();
}
