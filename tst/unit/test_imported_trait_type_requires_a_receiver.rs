//@ aux-build: trait_path_hygiene.rs
//@ compile-fail: type annotations needed
use trait_path_hygiene::Compute as Imported;
type Output = Imported<u8>::Result;
fn main() { let _: Output = 0u32; }
