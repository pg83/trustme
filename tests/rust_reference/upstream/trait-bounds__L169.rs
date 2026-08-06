// Extracted from src/trait-bounds.md:169
#![allow(unused)]
fn main() {
    fn requires_t_outlives_a<'a, T>(x: &'a T) {}
}
