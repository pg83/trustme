//@ compile-fail: #[ignore] can only be put on functions
#![deny(unused_attributes)]
#[ignore]
struct Skipped;
fn main() {}
