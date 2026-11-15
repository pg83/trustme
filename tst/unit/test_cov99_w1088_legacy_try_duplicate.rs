//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Duplicate definition of language item 'Try'
#![feature(lang_items)]

#[lang = "Try"]
pub trait LegacyTryFirst {}

#[lang = "Try"]
pub trait LegacyTrySecond {}
