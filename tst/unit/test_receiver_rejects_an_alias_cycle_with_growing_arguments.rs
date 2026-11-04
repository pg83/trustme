//@ compile-fail: Unknown receiver type
type Growing<T> = Growing<(T,)>;
struct Node;
impl Node { fn inspect(self: Growing<Self>) {} }
fn main() {}
