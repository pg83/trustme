//@ compile-fail: Trait was not a path

struct Marker;
impl (u8, u8) for Marker {}
fn main() {}
