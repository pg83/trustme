//@ compile-only
//@ compile-flags: -Zstop-after=parse

#![feature(never_patterns)]

enum Empty {}

struct Container {
    field: Empty,
}

fn consume(value: Container) {
    match value {
        Container { field: ! },
    }
}

fn main() {}
