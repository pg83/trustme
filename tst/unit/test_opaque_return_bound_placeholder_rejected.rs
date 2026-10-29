//@ compile-fail: the placeholder `_` is not allowed in function signatures
macro_rules! element { () => { _ }; }
fn elements() -> impl Iterator<Item = element!()> { [1u8, 2].into_iter() }
fn main() {}
