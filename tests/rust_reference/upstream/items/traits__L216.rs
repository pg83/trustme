// Extracted from src/items/traits.md:216
#![allow(unused)]
fn main() {
    trait Shape { fn area(&self) -> f64; }
    trait Circle where Self: Shape { fn radius(&self) -> f64; }
}
