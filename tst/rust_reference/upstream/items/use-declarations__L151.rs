// Extracted from src/items/use-declarations.md:151
#![allow(unused)]
fn main() {
    // Creates a non-public alias `bar` for the function `foo`.
    use inner::foo as bar;
    
    mod inner {
        pub fn foo() {}
    }
}
