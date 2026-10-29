//@ compile-fail: Unsize shouldn't be manually implemented
#![feature(unsize)]
struct Local;
impl std::marker::Unsize<str> for &Local {}
const TEXT: &str = &&Local;
fn main() { let _ = TEXT; }
