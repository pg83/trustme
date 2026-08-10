// Extracted from src/macros-by-example.md:88
#![allow(unused)]
fn main() {
    // compiles OK
    macro_rules! foo {
        ($l:tt) => { bar!($l); }
    }
    
    macro_rules! bar {
        (3) => {}
    }
    
    foo!(3);
}
