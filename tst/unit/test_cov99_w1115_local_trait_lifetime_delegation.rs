//@ crate-type: lib
//@ compile-flags: -Z stop-after=resolve -Z dump-ast
#![feature(fn_delegation)]
#![allow(incomplete_features)]

pub trait LifetimeSource {
    fn choose<'a: 'b, 'b>(value: &'a Self) -> &'b Self;
}

pub reuse LifetimeSource::choose as delegated_choose;
