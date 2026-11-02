//@ compile-fail: No macro arm matched

macro_rules! capture { ($ty:ty) => {}; }
capture!(impl for<'a> ~ Stamp<&'a u8>);

fn main() {}
