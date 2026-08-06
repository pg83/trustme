// Extracted from src/paths.md:392
fn foo() {}
mod a {
    fn bar() {
        crate::foo();
    }
}
fn main() {}
