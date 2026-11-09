//@ compile-fail: Conflicting definitions of lang item 'copy'
#![feature(no_core, lang_items)]
#![no_core]

extern crate core;

#[lang = "copy"]
trait LocalCopy {}

fn inspect(_value: core::marker::PhantomData<u8>) {}

fn main() {}
