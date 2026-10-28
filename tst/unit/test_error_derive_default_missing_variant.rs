//@ compile-fail: No #[default] attribute on enum
#[derive(Default)]
enum Value { A, B }
fn main() {}
