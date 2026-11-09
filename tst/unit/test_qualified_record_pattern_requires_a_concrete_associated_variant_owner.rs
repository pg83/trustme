//@ compile-fail: Expeted generic path in pattern binding
#![feature(more_qualified_paths)]

trait Repr {
    type Item;
}

fn inspect<T: Repr>(value: T::Item) {
    match value {
        <T::Item>::Missing { .. } => {}
    }
}

fn main() {}
