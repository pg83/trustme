// Extracted from library/core/src/hint.rs:367
#![allow(unused)]
fn main() {
    use std::hint::black_box;
    
    // Same `contains` function.
    fn contains(haystack: &[&str], needle: &str) -> bool {
        haystack.iter().any(|x| x == &needle)
    }
    
    pub fn benchmark() {
        let haystack = vec!["abc", "def", "ghi", "jkl", "mno"];
        let needle = "ghi";
        for _ in 0..10 {
            // Force the compiler to run `contains`, even though it is a pure function whose
            // results are unused.
            black_box(contains(
                // Prevent the compiler from making assumptions about the input.
                black_box(&haystack),
                black_box(needle),
            ));
        }
    }
}
