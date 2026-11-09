//@ compile-fail: error
#![feature(default_field_values)]

enum Entry {
    Empty,
    Other,
}

fn main() {
    let _entry = Entry::Empty { .. };
}
