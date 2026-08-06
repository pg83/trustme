// Extracted from library/core/src/str/mod.rs:1436
#![allow(unused)]
fn main() {
    let s = "Löwe 老虎 Léopard";
    
    assert_eq!(s.find(char::is_whitespace), Some(5));
    assert_eq!(s.find(char::is_lowercase), Some(1));
    assert_eq!(s.find(|c: char| c.is_whitespace() || c.is_lowercase()), Some(1));
    assert_eq!(s.find(|c: char| (c < 'o') && (c > 'a')), Some(4));
}
