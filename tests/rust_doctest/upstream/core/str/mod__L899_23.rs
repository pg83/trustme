// Extracted from library/core/src/str/mod.rs:899
#![allow(unused)]
fn main() {
    let s = "Per Martin-Löf";
    
    let (first, last) = s.split_at_checked(3).unwrap();
    assert_eq!("Per", first);
    assert_eq!(" Martin-Löf", last);
    
    assert_eq!(None, s.split_at_checked(13));  // Inside “ö”
    assert_eq!(None, s.split_at_checked(16));  // Beyond the string length
}
