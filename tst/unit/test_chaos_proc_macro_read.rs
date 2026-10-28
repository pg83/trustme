//@ chaos: proc-macro-read@0
//@ compile-fail: Error while reading from child process
//@ crate-type: lib
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

proc_macro_item_passthrough::echo!(pub fn answer() -> u32 { 42 });
