// Extracted from src/type-layout.md:130
#![allow(unused)]
fn main() {
    #[repr(C)]
    struct ThreeInts {
        first: i16,
        second: i8,
        third: i32
    }
}
