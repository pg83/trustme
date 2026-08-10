// Extracted from src/items/use-declarations.md:266
#![allow(unused)]
fn main() {
    // Creates a non-public alias to `bar`.
    use foo::*;
    
    mod foo {
        fn i_am_private() {}
        enum Example {
            V1,
            V2,
        }
        pub fn bar() {
            // Creates local aliases to `V1` and `V2`
            // of the `Example` enum.
            use Example::*;
            let x = V1;
        }
    }
}
