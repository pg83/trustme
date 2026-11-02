//@ compile-fail: No macro arm matched

macro_rules! capture { ($ty:ty) => {}; }
capture!(impl ~ Stamp);

fn main() {}
