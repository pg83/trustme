//@ edition: 2021
//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(async_drop, trivial_bounds, rustc_attrs)]
#![rustc_coherence_is_core]
#![allow(incomplete_features, internal_features)]

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

pub async fn discard(_value: [Element; 1])
where
    [Element; 1]: Drop,
{}
