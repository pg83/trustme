//@ aux-build: cov99_w524_shared_trait_alias.rs
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(trait_alias)]

use cov99_w524_shared_trait_alias::Shared as First;
use cov99_w524_shared_trait_alias::Shared as Second;

pub fn both<T: First + Second>(value: &T) -> u32 {
    value.number()
}
