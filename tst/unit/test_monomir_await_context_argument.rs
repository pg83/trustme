//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0 -Zdump-mir
//@ output-contains: .mir &mut (*arg1), ) goto
use std::future::Future;
use std::task::{Context, Poll};
async fn inner(value: u32) -> u32 { value + 1 }
pub fn poll_once(value: u32, context: &mut Context<'_>) -> Poll<u32> {
    let mut future = Box::pin(async move { inner(value).await + 2 });
    future.as_mut().poll(context)
}
