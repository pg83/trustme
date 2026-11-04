//@ compile-fail: Unknown receiver type
struct Node;
impl Node { fn inspect(self: (Self,)) {} }
fn main() {}
