//@ compile-fail: associated static items are not allowed
struct Invalid;
macro_rules! inject { ($item:item) => { impl Invalid { $item } }; }
inject!(static mut VALUE: u8 = 3;);
fn main() {}
