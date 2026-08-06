// Extracted from library/core/src/str/pattern.rs:19
#![allow(unused)]
fn main() {
    let s = "Can you find a needle in a haystack?";
    
    // &str pattern
    assert_eq!(s.find("you"), Some(4));
    // char pattern
    assert_eq!(s.find('n'), Some(2));
    // array of chars pattern
    assert_eq!(s.find(&['a', 'e', 'i', 'o', 'u']), Some(1));
    // slice of chars pattern
    assert_eq!(s.find(&['a', 'e', 'i', 'o', 'u'][..]), Some(1));
    // closure pattern
    assert_eq!(s.find(|c: char| c.is_ascii_punctuation()), Some(35));
}
