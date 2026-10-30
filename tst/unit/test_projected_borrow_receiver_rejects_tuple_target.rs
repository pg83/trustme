//@ compile-fail: Unknown receiver type
trait Family {
    type Item;
}

struct Value;

impl Family for Value {
    type Item = (Value,);
}

impl Value {
    fn invalid(self: &<Self as Family>::Item) {}
}

fn main() {}
