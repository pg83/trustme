//@ compile-fail: unused value that must be used
// A lint attribute on an item applies to that item, and an exact lint name
// beats a group whichever order the two were written in. Only the crate-level
// form was read before, so a `#[deny]` on a function did nothing.
//
// `#[must_use]` also has to travel with the item it is on: the discarded value
// here comes from another crate.
//
// Same shape as the Rust Reference example attributes/diagnostics.md:235.
#![allow(unused)]

// This allows all lints in the "unused" group, and the exact name overrides it.
#[allow(unused)]
#[deny(unused_must_use)]
fn denied() {
    // A `Result` from the standard library is `#[must_use]`.
    std::fs::remove_file("some_file_that_is_not_there"); //~ ERROR
}

fn main() {}
