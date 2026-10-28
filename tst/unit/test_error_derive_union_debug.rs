//@ compile-fail: Cannot derive(Debug) on union
#[derive(Debug)]
union Value { integer: u32, float: f32 }
fn main() {}
