//@ edition: 2021
//@ crate-type: lib
//@ compile-fail: Expected a struct behind the associated type in a pattern
#![feature(more_qualified_paths)]
#![allow(dead_code)]

trait Project {
    type Item;
}

struct Unit {}

fn rejected(value: Unit) {
    let <dyn Send<[u8; _]> as Project>::Item {} = value;
}
