// Extracted from src/trait-bounds.md:210
#![allow(unused)]
fn main() {
    struct Struct<'a, T> {
        // This requires `T: 'a` to be well-formed
        // which is inferred by the compiler.
        field: &'a T,
    }
    
    enum Enum<'a, T> {
        // This requires `T: 'a` to be well-formed,
        // which is inferred by the compiler.
        //
        // Note that `T: 'a` is required even when only
        // using `Enum::OtherVariant`.
        SomeVariant(&'a T),
        OtherVariant,
    }
    
    trait Trait<'a, T: 'a> {}
    
    // This would error because `T: 'a` is not implied by any type
    // in the impl header.
    //     impl<'a, T> Trait<'a, T> for () {}
    
    // This compiles as `T: 'a` is implied by the self type `&'a T`.
    impl<'a, T> Trait<'a, T> for &'a T {}
}
