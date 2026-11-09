//@ compile-fail: is not constrained by the impl trait
struct Token;

impl<T> Token {}

fn main() {}
