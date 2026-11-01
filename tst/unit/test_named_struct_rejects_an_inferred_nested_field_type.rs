//@ compile-fail: not allowed in item signatures
struct Inferred { value: Option<_> }
fn main() {}
