// Extracted from library/alloc/src/vec/mod.rs:3649
#![allow(unused)]
extern crate alloc;
fn main() {
    let v = vec!["a".to_string(), "b".to_string()];
    let mut v_iter = v.into_iter();
    
    let first_element: Option<String> = v_iter.next();
    
    assert_eq!(first_element, Some("a".to_string()));
    assert_eq!(v_iter.next(), Some("b".to_string()));
    assert_eq!(v_iter.next(), None);
}
