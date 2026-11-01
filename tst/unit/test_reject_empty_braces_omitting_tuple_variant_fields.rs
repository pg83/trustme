//@ compile-fail: pattern does not mention all fields
enum Choice { Tuple(u16), Empty }
fn main() { match Choice::Tuple(17) { Choice::Tuple {} => {}, Choice::Empty => {} } }
