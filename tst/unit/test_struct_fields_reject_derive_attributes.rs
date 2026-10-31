//@ compile-fail: #[derive] can only be applied
struct Record { #[derive(Clone)] value: u16 }
fn main() { let _ = Record { value: 53 }; }
