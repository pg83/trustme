//@ compile-fail: array repetition is not allowed in a destructuring assignment
fn main() { let mut output = 0u8; [output; 2] = [17, 23]; }
