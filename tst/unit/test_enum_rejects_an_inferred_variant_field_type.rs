//@ compile-fail: not allowed in item signatures
#[derive(Clone)]
enum Inferred { Value(Option<_>) }
fn main() {}
