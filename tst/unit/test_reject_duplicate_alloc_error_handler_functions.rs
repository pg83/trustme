//@ compile-fail: Duplicate definition of #[alloc_error_handler]
#![feature(alloc_error_handler)]
#[alloc_error_handler]
fn first(_: std::alloc::Layout) -> ! { loop {} }
#[alloc_error_handler]
fn second(_: std::alloc::Layout) -> ! { loop {} }
fn main() {}
