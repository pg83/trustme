//@ edition: 2015

#![feature(fn_delegation)]
#![allow(incomplete_features)]

mod functions {
    pub fn one() -> u8 { 1 }
    pub fn two() -> u8 { 2 }
}

reuse functions::{one as first, two};

trait Values {
    fn left(&self) -> u8;
    fn right(&self) -> u8;
}

impl Values for u8 {
    fn left(&self) -> u8 { *self }
    fn right(&self) -> u8 { *self + 1 }
}

mod helper {
    pub fn identity(value: &u8) -> &u8 { value }
    pub mod nested {}
}

macro_rules! field {
    ($receiver:ident) => { &$receiver.0 }
}

struct Listed(u8);

impl Values for Listed {
    reuse Values::{left, right} {
        use helper::{identity, nested::self};
        identity(field!(self))
    }
}

struct Globbed(u8);

impl Values for Globbed {
    reuse Values::* { self.0 }
}

fn main() {
    assert_eq!((first(), two()), (1, 2));
    assert_eq!((Values::left(&Listed(3)), Values::right(&Listed(3))), (3, 4));
    assert_eq!((Values::left(&Globbed(5)), Values::right(&Globbed(5))), (5, 6));
}
