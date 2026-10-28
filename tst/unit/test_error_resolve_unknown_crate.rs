//@ compile-fail: Unable to find external crate for path
fn main() { ::missing_crate::f(); }
