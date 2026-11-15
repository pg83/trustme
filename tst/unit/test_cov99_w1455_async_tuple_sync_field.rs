//@ compile-only
//@ edition: 2021
//@ compile-flags: -Cpanic=abort -Zstop-after=mir -Zdump-mir
#![feature(async_drop)]

use std::future::AsyncDrop;
use std::pin::Pin;

struct SyncField;

impl Drop for SyncField {
    fn drop(&mut self) {}
}

struct AsyncTuple(SyncField);

impl Drop for AsyncTuple {
    fn drop(&mut self) {}
}

impl AsyncDrop for AsyncTuple {
    async fn drop(self: Pin<&mut Self>) {}
}

async fn leave_scope() {
    let _owner = AsyncTuple(SyncField);
}

fn main() {
    let _future = leave_scope();
}
