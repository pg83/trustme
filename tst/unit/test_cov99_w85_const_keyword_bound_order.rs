//@ crate-type: lib
//@ compile-flags: -Zdump-hir
#![feature(const_trait_impl)]
#![allow(dead_code)]

const trait Witness {}

impl const Witness for u8 {}

macro_rules! environments {
    ([] [$($bounds:tt)*]) => {
        const fn check<T>() -> u8
        where
            T: $($bounds)*
        {
            85
        }
    };
    ([$head:tt $($tail:tt)*] [$($bounds:tt)*]) => {
        mod plain {
            environments!([$($tail)*] [$($bounds)* crate::Witness +]);
        }
        mod conditional {
            environments!([$($tail)*] [$($bounds)* [const] crate::Witness +]);
        }
    };
}

environments!([x x x x x x x x] []);
