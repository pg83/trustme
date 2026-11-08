//@ compile-fail: is not allowed in an extern block
macro_rules! foreign { ($item:item) => { extern "C" { $item } }; }
foreign!(struct Packet { tag: u8 });
fn main() {}
