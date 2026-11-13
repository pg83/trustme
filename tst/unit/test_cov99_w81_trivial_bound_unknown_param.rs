//@ compile-fail: is not satisfied

trait Mark<U> {}

struct Needs where u8: Mark<_>;

fn main() {}
