//@ edition: 2021
//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(async_drop, trivial_bounds)]
#![allow(incomplete_features)]

use std::future::AsyncDrop;
use std::pin::Pin;

pub struct Element;
impl Drop for Element {
    fn drop(&mut self) {}
}

impl AsyncDrop for [Element; 1]
where
    [Element; 1]: Drop,
{
    async fn drop(self: Pin<&mut Self>) {}
}

pub trait Witness<T>: Drop {}

pub async fn discard<T>(_value: [Element; 1], _witness: T)
where
    [Element; 1]: Witness<T>,
{}
