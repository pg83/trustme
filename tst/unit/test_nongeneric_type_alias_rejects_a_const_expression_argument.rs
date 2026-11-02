//@ compile-fail: generic
type Bytes = [u8; 3];
fn main() { let _: Bytes<{ 2 + 1 }> = [1, 2, 3]; }
