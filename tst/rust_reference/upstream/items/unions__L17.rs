// Extracted from src/items/unions.md:17
#![allow(unused)]
fn main() {
    #[repr(C)]
    union MyUnion {
        f1: u32,
        f2: f32,
    }
}
