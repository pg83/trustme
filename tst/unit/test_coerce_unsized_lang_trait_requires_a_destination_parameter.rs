//@ compile-fail: Unexpected number of arguments for CoerceUnsized
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "coerce_unsized"]
trait Shift {}

struct Shell(u8);

impl Shift for Shell {}

fn main() {}
