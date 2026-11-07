//@ crate-type: lib
//@ compile-fail: Duplicate definition of language item 'unpin'
#![feature(no_core, lang_items, auto_traits)]
#![no_core]

#[lang = "unpin"]
pub auto trait FirstUnpin {}

#[lang = "unpin"]
pub auto trait SecondUnpin {}
