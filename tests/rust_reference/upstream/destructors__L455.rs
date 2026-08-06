// Extracted from src/destructors.md:455
#![allow(unused)]
fn main() {
    fn temp() {}
    // This is an extending pattern, so the temporary scope is extended.
    let ref x = *&temp(); // OK
    x;
}
