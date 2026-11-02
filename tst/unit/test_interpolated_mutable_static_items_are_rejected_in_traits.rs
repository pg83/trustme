//@ compile-fail: static
macro_rules! declare { ($member:item) => { trait Record { $member } } }
declare! { static mut VALUE: u32 = 17; }
fn main() {}
