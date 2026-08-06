// Extracted from src/attributes/codegen.md:762
#![allow(unused)]
fn main() {
    #[track_caller]
    fn f() {
        println!("{}", std::panic::Location::caller());
    }
}
