//@ compile-fail: static
macro_rules! implement {
    ($member:item) => {
        trait Record {}
        struct Leaf;
        impl Record for Leaf { $member }
    }
}
implement! { static VALUE: u32 = 17; }
fn main() {}
