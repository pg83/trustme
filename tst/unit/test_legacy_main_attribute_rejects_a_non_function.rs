//@ compile-fail: #[main] on non-function
#[main]
struct Entry;
fn main() {}
