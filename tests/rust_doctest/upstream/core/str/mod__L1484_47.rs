// Extracted from library/core/src/str/mod.rs:1484
#![allow(unused)]
fn main() {
    let s = "Löwe 老虎 Léopard";

    assert_eq!(s.rfind(char::is_whitespace), Some(12));
    assert_eq!(s.rfind(char::is_lowercase), Some(20));
}
