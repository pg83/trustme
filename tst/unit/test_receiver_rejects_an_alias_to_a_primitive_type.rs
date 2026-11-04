//@ compile-fail: Unknown receiver type
mod wrong { pub type Handle = u16; }
struct Node;
impl Node { fn inspect(self: wrong::Handle) {} }
fn main() {}
