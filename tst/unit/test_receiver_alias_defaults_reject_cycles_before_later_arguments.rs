//@ compile-fail: Unknown receiver type
type Loop<T = Loop> = T;
type Pair<A, B> = (A, B);
struct Node;
impl Node { fn inspect(self: Pair<Loop, u16>) {} }
fn main() {}
