//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types)]
struct Node;
impl Node { fn inspect(self: &&(Self,)) {} }
fn main() {}
