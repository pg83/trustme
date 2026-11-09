//@ compile-fail: didn't point to a struct or union (Enum)
#![feature(more_qualified_paths)]

enum Shape {
    Point(u8),
}

trait Carrier {
    type Item;
}

impl Carrier for () {
    type Item = Shape;
}

fn inspect(value: Shape) {
    match value {
        <() as Carrier>::Item { .. } => {}
    }
}

fn main() {}
