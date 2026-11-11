//@ proc-macro-aux-build: proc_macro_closes_startup_output.rs
//@ compile-fail: proc_macro expansion failed

#[proc_macro_closes_startup_output::preserve]
fn value() -> u16 { 43 }

fn main() { assert_eq!(value(), 43); }
