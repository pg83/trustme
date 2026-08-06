// Extracted from library/core/src/str/mod.rs:1259
#![allow(unused)]
fn main() {
    let text = "foo\r\nbar\n\nbaz\r";
    let mut lines = text.lines();
    
    assert_eq!(Some("foo"), lines.next());
    assert_eq!(Some("bar"), lines.next());
    assert_eq!(Some(""), lines.next());
    // Trailing carriage return is included in the last line
    assert_eq!(Some("baz\r"), lines.next());
    
    assert_eq!(None, lines.next());
}
