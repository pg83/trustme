// Extracted from library/core/src/hint.rs:770
#![allow(unused)]
fn main() {
    use std::hash::BuildHasher;
    use std::hint;
    
    fn append<H: BuildHasher>(hasher: &H, v: i32, bucket_one: &mut Vec<i32>, bucket_two: &mut Vec<i32>) {
        let hash = hasher.hash_one(&v);
        let bucket = hint::select_unpredictable(hash % 2 == 0, bucket_one, bucket_two);
        bucket.push(v);
    }
    let hasher = std::collections::hash_map::RandomState::new();
    let mut bucket_one = Vec::new();
    let mut bucket_two = Vec::new();
    append(&hasher, 42, &mut bucket_one, &mut bucket_two);
    assert_eq!(bucket_one.len() + bucket_two.len(), 1);
}
