// Extracted from library/core/src/hint.rs:339
#![allow(unused)]
fn main() {
    fn contains(haystack: &[&str], needle: &str) -> bool {
        haystack.iter().any(|x| x == &needle)
    }

    pub fn benchmark() {
        let haystack = vec!["abc", "def", "ghi", "jkl", "mno"];
        let needle = "ghi";
        for _ in 0..10 {
            contains(&haystack, needle);
        }
    }
}
