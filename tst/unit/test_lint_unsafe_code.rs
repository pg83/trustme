//@ compile-fail: usage of an `unsafe` block
// The `unsafe_code` lint reports every use of `unsafe`. It is `allow` by
// default, so `#[warn(unsafe_code)]` turns it on for one item -- and
// `#[deny(warnings)]` then lifts it, because `warnings` applies to whatever is
// still at warn whichever order the two attributes were written in.
//
// Same shape as the Rust Reference example attributes/diagnostics.md:256.
#![allow(unused)]

unsafe fn anUnsafeFn() {}

// The order of these two attributes does not matter.
#[deny(warnings)]
#[warn(unsafe_code)]
fn exampleErr() {
    unsafe { anUnsafeFn() } //~ ERROR
}

// Without the attributes the lint stays quiet.
fn exampleOk() {
    unsafe { anUnsafeFn() }
}

fn main() {}
