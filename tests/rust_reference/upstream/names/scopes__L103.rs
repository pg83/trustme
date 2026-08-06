// Extracted from src/names/scopes.md:103
#![allow(unused)]
fn main() {
    trait SomeTrait<'a, T> {}
    // The <'a, U> for `SomeTrait` refer to the 'a and U parameters of `bounds_scope`.
    fn bounds_scope<'a, T: SomeTrait<'a, U>, U>() {}
    
    fn where_scope<'a, T, U>()
        where T: SomeTrait<'a, U>
    {}
}
