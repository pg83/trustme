// Extracted from src/trait-bounds.md:178
#![allow(unused)]
fn main() {
    fn requires_t_outlives_a_not_implied<'a, T: 'a>() {}
    
    fn requires_t_outlives_a<'a, T>(x: &'a T) {
        // This compiles, because `T: 'a` is implied by
        // the reference type `&'a T`.
        requires_t_outlives_a_not_implied::<'a, T>();
    }
}
