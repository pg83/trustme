//@ compile-fail: the placeholder `_` is not allowed in function signatures
fn accepts(_: Option<_>) {}
fn main() {}
