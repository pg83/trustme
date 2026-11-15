//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Duplicate definition of language item 'panic_fmt'

#![feature(lang_items)]

#[lang = "panic_fmt"]
fn first(_: core::fmt::Arguments<'_>) -> ! {
    loop {}
}

#[lang = "panic_fmt"]
fn second(_: core::fmt::Arguments<'_>) -> ! {
    loop {}
}
