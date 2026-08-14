//@ crate-type: lib

#![feature(trivial_bounds)]
#![allow(trivial_bounds)]

trait Required {}

pub trait Target {
    fn method();
}

impl Target for u8
where
    u8: Required,
{
    fn method() {}
}
