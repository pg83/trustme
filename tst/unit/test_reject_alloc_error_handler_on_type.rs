//@ compile-fail: #[alloc_error_handler] on non-function
#![feature(alloc_error_handler)]
#[alloc_error_handler]
struct Wrong;
fn main() {}
