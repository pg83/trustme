//@ compile-fail: Empty ABI
type Callback = extern "" fn();
fn main() {}
