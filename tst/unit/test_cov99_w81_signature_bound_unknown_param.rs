//@ compile-fail: the placeholder `_` is not allowed in function signatures

trait Mark<U> {}

impl Mark<u8> for u8 {}

struct Needs<T: Mark<U>, U>(T, U);

fn inspect(_: Needs<u8, _>) {}

fn main() {}
