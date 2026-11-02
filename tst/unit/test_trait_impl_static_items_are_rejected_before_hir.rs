//@ compile-fail: static
trait Record {}
struct Leaf;
impl Record for Leaf { static VALUE: u32 = 17; }
fn main() {}
