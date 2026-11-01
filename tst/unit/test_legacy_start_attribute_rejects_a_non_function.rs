//@ compile-fail: #[start] on non-function
#[start]
struct Entry;
fn main() {}
