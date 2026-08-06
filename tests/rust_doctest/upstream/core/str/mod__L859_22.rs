// Extracted from library/core/src/str/mod.rs:859
#![allow(unused)]
fn main() {
    let mut s = "Per Martin-Löf".to_string();
    {
        let (first, last) = s.split_at_mut(3);
        first.make_ascii_uppercase();
        assert_eq!("PER", first);
        assert_eq!(" Martin-Löf", last);
    }
    assert_eq!("PER Martin-Löf", s);
}
