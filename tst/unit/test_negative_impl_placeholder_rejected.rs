//@ compile-fail: the placeholder `_` is not allowed in impl signatures
#![feature(negative_impls)]
struct Wrap<T>(T);
impl !Send for Wrap<_> {}
fn main() {}
