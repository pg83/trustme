// Extracted from src/destructors.md:470
#![allow(unused)]
fn main() {
    fn temp() {}
    // This is not an extending pattern but it is an extending expression,
    // so the temporary lives beyond the `let` statement.
    let &ref x = &*&temp(); // OK
    x;
}
