//@ crate-type: lib
//@ compile-fail: Duplicate definition of language item 'send'
#![feature(no_core, lang_items, auto_traits)]
#![no_core]

#[lang = "send"]
pub unsafe auto trait FirstSend {}

#[lang = "send"]
pub unsafe auto trait SecondSend {}
