//@ compile-fail: #[derive] can only be applied
enum Choice { #[derive(Clone)] Value(u16) }
fn main() { let _ = Choice::Value(53); }
