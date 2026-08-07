// Extracted from library/core/src/str/mod.rs:938
#![allow(unused)]
fn main() {
    let mut s = "Per Martin-Löf".to_string();
    if let Some((first, last)) = s.split_at_mut_checked(3) {
        first.make_ascii_uppercase();
        assert_eq!("PER", first);
        assert_eq!(" Martin-Löf", last);
    }
    assert_eq!("PER Martin-Löf", s);

    assert_eq!(None, s.split_at_mut_checked(13));  // Inside “ö”
    assert_eq!(None, s.split_at_mut_checked(16));  // Beyond the string length
}
