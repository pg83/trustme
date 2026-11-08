//@ compile-fail: Duplicate definition of #[global_allocator]
#[global_allocator]
static FIRST: std::alloc::System = std::alloc::System;
#[global_allocator]
static SECOND: std::alloc::System = std::alloc::System;
fn main() {}
