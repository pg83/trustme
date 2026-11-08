//@ compile-fail: #[thread_local] on
#![feature(thread_local)]

#[thread_local]
fn main() {}
