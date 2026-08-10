// Extracted from src/items/use-declarations.md:188
mod stuff {
    pub fn foo() {}
    pub fn bar() {}
}
mod example {
    // Creates a binding for `stuff` and `foo`.
    use crate::stuff::{self, foo};
    pub fn baz() {
        foo();
        stuff::bar();
    }
}
fn main() {}
