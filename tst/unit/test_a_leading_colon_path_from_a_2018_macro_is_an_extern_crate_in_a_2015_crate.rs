//@ edition: 2015
//@ proc-macro-aux-build: emits_leading_colon_use.rs
// A path's leading `::` is read in the edition of its first segment's span,
// not the crate's: the tokens of a 2018 proc macro name the extern prelude
// with `::std` even inside a 2015 crate.
#[macro_use]
extern crate emits_leading_colon_use;

writer_module!();

fn main() {
    assert_eq!(writer::hello(), "hello");
}
