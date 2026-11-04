//@ compile-fail: mismatching literal classes
fn same<T>(_: T, _: T) {}
fn main() { same(181, 3.25); }
