//@ compile-fail: mismatching literal classes
fn same<T>(_: T, _: T) {}
fn main() { same(3.25, 181); }
