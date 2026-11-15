//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(async_drop)]
#![allow(incomplete_features, drop_bounds)]

use std::future::AsyncDrop;
use std::pin::Pin;

pub trait Local: Drop {}

impl AsyncDrop for dyn Local {
    async fn drop(self: Pin<&mut Self>) {}
}

impl AsyncDrop for Box<dyn Local> {
    async fn drop(self: Pin<&mut Self>) {}
}

pub async fn consume(_value: Box<dyn Local>) {}
