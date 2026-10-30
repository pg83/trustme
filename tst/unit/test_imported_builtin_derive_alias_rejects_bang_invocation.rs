//@ aux-build: derive_type_aliases.rs
//@ compile-fail: Builtin derive macros can only be used in #[derive]
use derive_type_aliases::Printable;
Printable!();
fn main() {}
