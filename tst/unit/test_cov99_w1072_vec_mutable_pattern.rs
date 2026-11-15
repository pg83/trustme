//@ crate-type: lib
//@ compile-flags: -Z stop-after=typeck
#![feature(deref_patterns)]
#![allow(incomplete_features)]

pub fn replace_first(mut values: Vec<i32>) -> Vec<i32> {
    match values {
        [ref mut first, ..] => *first = 29,
        _ => {}
    }
    values
}
