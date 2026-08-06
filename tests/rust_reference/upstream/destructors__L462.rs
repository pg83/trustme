// Extracted from src/destructors.md:462
#![allow(unused)]
fn main() {
    fn temp() {}
    // This is neither an extending pattern nor an extending expression,
    // so the temporary is dropped at the semicolon.
    let &ref x = *&&temp(); // ERROR
    x;
}
