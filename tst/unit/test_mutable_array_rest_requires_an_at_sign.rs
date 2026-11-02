//@ compile-fail: Unexpected token
fn main() { let [mut tail..] = [1u8, 2, 3]; }
