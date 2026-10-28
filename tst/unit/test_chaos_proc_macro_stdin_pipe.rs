//@ chaos: proc-macro-pipe@0
//@ compile-fail: Unable to create stdin pipe pair
//@ crate-type: lib
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

proc_macro_item_passthrough::echo!(pub fn answer() -> u32 { 42 });
