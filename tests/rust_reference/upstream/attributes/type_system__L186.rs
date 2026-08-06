// Extracted from src/attributes/type_system.md:186
#![allow(unused)]
fn main() {
    #[non_exhaustive]
    pub enum EnumWithNonExhaustiveVariants {
        First,
        #[non_exhaustive]
        Second,
    }
}
