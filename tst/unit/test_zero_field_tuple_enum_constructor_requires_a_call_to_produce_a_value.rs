//@ compile-fail: Type mismatch

enum Choice { Empty() }
fn main() { let _: Choice = Choice::Empty; }
