//@ compile-fail: Byte string pattern has length
fn main() { let _ = match &[1u8, 2, 3] { b"hi" => true, _ => false }; }
