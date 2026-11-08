//@ compile-fail: Multiple `use` seen in erased type
fn value() -> impl Copy + use<> + use<> { 3u8 }
fn main() {}
