//@ compile-fail: Multiple #[default] attributes
#[derive(Default)]
enum Value { #[default] A, #[default] B }
fn main() {}
