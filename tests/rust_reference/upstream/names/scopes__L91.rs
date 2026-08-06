// Extracted from src/names/scopes.md:91
#![allow(unused)]
fn main() {
    // The 'b bound is referenced before it is declared.
    fn params_scope<'a: 'b, 'b>() {}
    
    trait SomeTrait<const Z: usize> {}
    // The const N is referenced in the trait bound before it is declared.
    fn f<T: SomeTrait<N>, const N: usize>() {}
}
