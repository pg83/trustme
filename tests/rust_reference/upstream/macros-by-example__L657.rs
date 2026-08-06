// Extracted from src/macros-by-example.md:657
#![allow(unused)]
fn main() {
    pub mod inner {
        #[macro_export]
        macro_rules! call_foo {
            () => { $crate::inner::foo() };
        }
    
        pub fn foo() {}
    }
}
