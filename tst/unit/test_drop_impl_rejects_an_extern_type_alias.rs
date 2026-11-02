//@ compile-fail: Drop can only be implemented for structs, enums, and unions
#![feature(extern_types)]
unsafe extern "C" { type Foreign; }
type Alias = Foreign;
impl Drop for Alias { fn drop(&mut self) {} }
fn main() {}
