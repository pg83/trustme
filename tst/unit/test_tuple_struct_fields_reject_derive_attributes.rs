//@ compile-fail: #[derive] can only be applied
struct Record(#[derive(Clone)] u16);
fn main() { let _ = Record(53); }
