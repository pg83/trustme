//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types_pointers)]

trait Family {
    type Item;
}

struct Value;

impl Family for Value {
    type Item = (Value,);
}

impl Value {
    fn invalid(self: *const <Self as Family>::Item) {}
}

fn main() {}
