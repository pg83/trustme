//@ compile-fail: error
#![feature(default_field_values)]

enum Entry {
    Empty,
    Value(u8),
}

fn main() {
    let _entry = Entry::Empty { .. };
}
