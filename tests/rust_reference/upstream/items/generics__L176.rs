// Extracted from src/items/generics.md:176
#![allow(unused)]
fn main() {
    fn f<const N: usize>(x: [u8; N]) -> [u8; _] { x }
    //                                       ^ ERROR not allowed
}
