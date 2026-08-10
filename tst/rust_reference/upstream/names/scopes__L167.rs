// Extracted from src/names/scopes.md:167
#![allow(unused)]
fn main() {
    trait Trait<'a>{}
    
    fn where_clause<T>()
        // 'a is in scope in both the type and the type bounds.
        where for <'a> &'a T: Trait<'a>
    {}
    
    fn bound<T>()
        // 'a is in scope within the bound.
        where T: for <'a> Trait<'a>
    {}
    
    struct Example<'a> {
        field: &'a u32
    }
    
    // 'a is in scope in both the parameters and return type.
    type FnExample = for<'a> fn(x: Example<'a>) -> Example<'a>;
}
