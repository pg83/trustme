//@ compile-fail: not allowed in item signatures
#[derive(Clone)]
struct Inferred(_);
fn main() {}
