//@ compile-fail: Unknown receiver type
type Cycle = Cycle;
struct Node;
impl Node { fn inspect(self: Cycle) {} }
fn main() {}
