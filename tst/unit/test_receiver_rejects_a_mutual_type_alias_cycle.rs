//@ compile-fail: Unknown receiver type
type First<T> = Second<T>;
type Second<T> = First<T>;
struct Node;
impl Node { fn inspect(self: First<Self>) {} }
fn main() {}
