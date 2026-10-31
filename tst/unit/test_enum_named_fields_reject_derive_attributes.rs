//@ compile-fail: #[derive] can only be applied
enum Choice { Value { #[derive(Clone)] value: u16 } }
fn main() { let _ = Choice::Value { value: 53 }; }
