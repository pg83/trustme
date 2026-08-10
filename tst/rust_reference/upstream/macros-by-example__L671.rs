// Extracted from src/macros-by-example.md:671
#![allow(unused)]
fn main() {
    #[macro_export]
    macro_rules! call_foo {
        () => { $crate::foo() };
    }
    
    fn foo() {}
}
