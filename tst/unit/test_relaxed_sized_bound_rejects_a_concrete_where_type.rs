//@ compile-fail: relaxed bounds are only permitted on type parameters
fn invalid() where u32: ?Sized {}
fn main() { invalid(); }
