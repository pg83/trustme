//@ compile-fail: Use of an erased type outside of a function return
#[derive(Clone)]
struct Invalid { value: impl Clone }
fn main() {}
