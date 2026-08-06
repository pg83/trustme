// Extracted from src/trait-bounds.md:188
#![allow(unused)]
fn main() {
    fn requires_t_outlives_a_not_implied<'a, T: 'a>() {}
    fn not_implied<'a, T>() {
        // This errors, because `T: 'a` is not implied by
        // the function signature.
        requires_t_outlives_a_not_implied::<'a, T>();
    }
}
