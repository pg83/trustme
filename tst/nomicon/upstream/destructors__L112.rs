// Extracted from src/destructors.md:112
#![allow(unused)]
fn main() {
    enum Link {
        Next(Box<Link>),
        None,
    }
}
