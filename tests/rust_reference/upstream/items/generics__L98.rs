// Extracted from src/items/generics.md:98
#![allow(unused)]
fn main() {
    // Examples where const generic parameters cannot be used.
    fn foo<const N: usize>() {
        // Cannot use in item definitions within a function body.
        const BAD_CONST: [usize; N] = [1; N];
        static BAD_STATIC: [usize; N] = [1; N];
        fn inner(bad_arg: [usize; N]) {
            let bad_value = N * 2;
        }
        type BadAlias = [usize; N];
        struct BadStruct([usize; N]);
    }
}
