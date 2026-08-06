// Extracted from src/fn/diverging.md:6
#![allow(unused)]
fn main() {
    fn foo() -> ! {
        panic!("This call never returns.");
    }
}
