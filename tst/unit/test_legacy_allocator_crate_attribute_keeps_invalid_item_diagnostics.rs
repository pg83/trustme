//@ compile-fail: #[allocator] can only be put on functions and the crate
#![allocator]
#[allocator]
struct WrongAllocator;
fn main() {}
