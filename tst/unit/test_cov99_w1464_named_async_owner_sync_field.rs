//@ crate-type: lib
//@ compile-flags: -Zstop-after=mir -Zdump-mir -Cpanic=abort
#![feature(async_drop)]

use std::future::AsyncDrop;
use std::pin::Pin;

struct SyncField;

impl Drop for SyncField {
    fn drop(&mut self) {}
}

struct NamedOwner {
    synchronous: SyncField,
}

impl Drop for NamedOwner {
    fn drop(&mut self) {}
}

impl AsyncDrop for NamedOwner {
    async fn drop(self: Pin<&mut Self>) {}
}

pub async fn named_owner_scope() {
    let _owner = NamedOwner { synchronous: SyncField };
}
