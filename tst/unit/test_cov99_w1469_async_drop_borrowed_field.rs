//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(async_drop, trivial_bounds)]
#![allow(drop_bounds)]

use std::future::AsyncDrop;
use std::pin::Pin;

pub struct Borrowed;
pub struct Holder(&'static mut Borrowed);

impl Drop for Holder {
    fn drop(&mut self) {}
}

impl AsyncDrop for Holder {
    async fn drop(self: Pin<&mut Self>) {}
}

impl AsyncDrop for &'static mut Borrowed
where
    &'static mut Borrowed: Drop,
{
    async fn drop(self: Pin<&mut Self>) {}
}

pub async fn consume(_value: Holder)
where
    &'static mut Borrowed: Drop,
{
}
