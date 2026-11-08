//@ compile-fail: Union constructors can only specify a single field
union Word { first: u8, second: u16 }
fn main() { let _ = Word { first: 3, second: 5 }; }
