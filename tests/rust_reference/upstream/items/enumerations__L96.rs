// Extracted from src/items/enumerations.md:96
#![allow(unused)]
fn main() {
    enum Examples {
        UnitLike,
        TupleLike(i32),
        StructLike { value: i32 },
    }
    
    use Examples::*; // Creates aliases to all variants.
    let x = UnitLike; // Path expression of the const item.
    let x = UnitLike {}; // Struct expression.
    let y = TupleLike(123); // Call expression.
    let y = TupleLike { 0: 123 }; // Struct expression using integer field names.
    let z = StructLike { value: 123 }; // Struct expression.
}
