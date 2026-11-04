//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types_pointers)]
struct Node;
impl Node { fn inspect(self: *const (Self,)) {} }
fn main() {}
