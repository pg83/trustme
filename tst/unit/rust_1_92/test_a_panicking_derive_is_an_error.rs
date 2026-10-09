//@ proc-macro-aux-build: panicking_derive.rs
//@ compile-fail: = help: message: Shape cannot derive Refused
// asn1-rs's derive panics on a tag given twice. rustc's bridge runs the
// macro under `catch_unwind`, hides the panic's own output while the macro
// runs, and reports "proc-macro derive panicked" with the payload's text as
// help. Our macro host died with the panic and the compiler stopped on an
// unexpected end of the host's output.
use panicking_derive::Refused;

#[derive(Refused)]
struct Shape;

fn main() {}
