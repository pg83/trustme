//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(async_drop)]
#![allow(incomplete_features)]

pub struct Resource;

impl Drop for Resource {
    fn drop(&mut self) {}
}

impl std::future::AsyncDrop for Resource {
    async fn drop(self: std::pin::Pin<&mut Self>) {}
}

pub async fn discard<const N: usize>(values: [Resource; N]) {
    let _held = values;
}
