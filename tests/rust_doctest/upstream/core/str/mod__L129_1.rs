// Extracted from library/core/src/str/mod.rs:129
#![allow(unused)]
fn main() {
    let len = "foo".len();
    assert_eq!(3, len);
    
    assert_eq!("ƒoo".len(), 4); // fancy f!
    assert_eq!("ƒoo".chars().count(), 3);
}
