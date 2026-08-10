// Extracted from src/patterns.md:905
#![allow(unused)]
fn main() {
    let int_reference = &3;
    match int_reference {
        &(0..=5) => (),
        _ => (),
    }
}
