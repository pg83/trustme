//@ compile-fail: associated item constraints are not allowed here
struct Holder<T>(T);
#[derive(Clone)]
struct Invalid { value: Holder<Item: Clone> }
fn main() {}
