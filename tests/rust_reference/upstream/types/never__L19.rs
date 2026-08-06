// Extracted from src/types/never.md:19
#![allow(unused)]
fn main() {
    fn foo() -> ! {
        panic!("This call never returns.");
    }
}
