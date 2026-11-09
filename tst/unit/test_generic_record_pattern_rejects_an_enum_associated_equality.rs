//@ compile-fail: Pattern associated type didn't resolve to a struct or union
#![feature(more_qualified_paths)]

enum Shape {
    Point(u8),
}

trait Carrier {
    type Item;
}

fn inspect<T: Carrier<Item = Shape>>(value: Shape) {
    match value {
        T::Item { .. } => {}
    }
}

fn main() {}
