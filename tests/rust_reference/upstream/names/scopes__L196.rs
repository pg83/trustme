// Extracted from src/names/scopes.md:196
#![allow(unused)]
fn main() {
    trait Trait1 {
        type Item;
    }
    trait Trait2<'a> {}
    
    struct Example;
    
    impl Trait1 for Example {
        type Item = Element;
    }
    
    struct Element;
    impl<'a> Trait2<'a> for Element {}
    
    // The `impl Trait2` here is not allowed to refer to 'b but it is allowed to
    // refer to 'a.
    fn foo<'a>() -> impl for<'b> Trait1<Item = impl Trait2<'a> + use<'a>> {
        // ...
       Example
    }
}
