// Extracted from src/crates-and-source-files.md:80
#![allow(unused)]
fn main() {
    mod foo {
        pub fn bar() {
            println!("Hello, world!");
        }
    }
    use foo::bar as main;
}
