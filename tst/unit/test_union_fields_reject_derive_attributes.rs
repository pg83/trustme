//@ compile-fail: #[derive] can only be applied
union Bits { #[derive(Clone)] value: u16 }
fn main() { let _ = Bits { value: 53 }; }
