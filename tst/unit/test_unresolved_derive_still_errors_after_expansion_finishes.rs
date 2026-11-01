//@ compile-fail: Missing handlers for
#[derive(Clone, Missing)]
struct Record(u16);
fn main() {}
