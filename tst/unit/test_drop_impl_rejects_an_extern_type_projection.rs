//@ compile-fail: Drop can only be implemented for structs, enums, and unions
#![feature(extern_types)]
unsafe extern "C" { type Foreign; }
trait Family { type Item: ?Sized; }
struct Kind;
impl Family for Kind { type Item = Foreign; }
impl Drop for <Kind as Family>::Item { fn drop(&mut self) {} }
fn main() {}
