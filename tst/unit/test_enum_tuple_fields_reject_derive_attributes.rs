//@ compile-fail: #[derive] can only be applied
enum Choice { Value(#[derive(Clone)] u16) }
fn main() { let _ = Choice::Value(53); }
