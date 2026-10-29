//@ compile-fail: associated static items are not allowed
macro_rules! inject { ($item:item) => { trait Invalid { $item } }; }
inject!(static VALUE: u8 = 3;);
fn main() {}
