//@ compile-fail: associated item constraints are not allowed here
struct Holder<T>(T);
#[derive(Clone)]
struct Invalid<T> { value: Holder<Item = T> }
fn main() {}
