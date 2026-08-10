// Extracted from src/items/enumerations.md:135
#![allow(unused)]
fn main() {
    #[repr(u8)]
       enum Enum {
           Unit = 3,
           Tuple(u16),
           Struct {
               a: u8,
               b: u16,
           } = 1,
       }
}
