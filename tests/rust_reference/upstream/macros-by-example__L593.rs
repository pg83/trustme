// Extracted from src/macros-by-example.md:593
#![allow(unused)]
fn main() {
    let x = 1;
    fn func() {
        unreachable!("this is never called")
    }
    
    macro_rules! check {
        () => {
            assert_eq!(x, 1); // Uses `x` from the definition site.
            func();           // Uses `func` from the invocation site.
        };
    }
    
    {
        let x = 2;
        fn func() { /* does not panic */ }
        check!();
    }
}
