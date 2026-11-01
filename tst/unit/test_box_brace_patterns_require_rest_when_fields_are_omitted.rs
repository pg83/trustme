//@ compile-fail: pattern does not mention all fields
fn main() { let Box {} = Box::new(31u16); }
