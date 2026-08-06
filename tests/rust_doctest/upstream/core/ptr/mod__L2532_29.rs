// Extracted from library/core/src/ptr/mod.rs:2532
#![allow(unused)]
fn main() {
    use std::hash::{DefaultHasher, Hash, Hasher};
    use std::ptr;
    
    let five = 5;
    let five_ref = &five;
    
    let mut hasher = DefaultHasher::new();
    ptr::hash(five_ref, &mut hasher);
    let actual = hasher.finish();
    
    let mut hasher = DefaultHasher::new();
    (five_ref as *const i32).hash(&mut hasher);
    let expected = hasher.finish();
    
    assert_eq!(actual, expected);
}
