//@ compile-fail: multiple rest expressions in a destructuring assignment
fn main() { let mut output = 0u8; [output, .., ..] = [17, 23, 31]; }
