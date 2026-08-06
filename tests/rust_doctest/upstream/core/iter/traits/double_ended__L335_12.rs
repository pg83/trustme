// Extracted from library/core/src/iter/traits/double_ended.rs:335
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    
    assert_eq!(a.iter().rfind(|&&x| x == 2), Some(&2));
    
    assert_eq!(a.iter().rfind(|&&x| x == 5), None);
}
