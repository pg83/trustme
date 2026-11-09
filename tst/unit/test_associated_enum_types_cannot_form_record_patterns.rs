//@ compile-fail: struct or union
#![feature(more_qualified_paths)]

enum Choice {
    Selected,
}

trait Schema {
    type Item;
}

impl Schema for () {
    type Item = Choice;
}

fn main() {
    match Choice::Selected {
        <() as Schema>::Item {} => (),
        _ => (),
    }
}
