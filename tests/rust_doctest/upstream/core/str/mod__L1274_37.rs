// Extracted from library/core/src/str/mod.rs:1274
#![allow(unused)]
fn main() {
    let text = "foo\nbar\n\r\nbaz";
    let mut lines = text.lines();
    
    assert_eq!(Some("foo"), lines.next());
    assert_eq!(Some("bar"), lines.next());
    assert_eq!(Some(""), lines.next());
    assert_eq!(Some("baz"), lines.next());
    
    assert_eq!(None, lines.next());
}
