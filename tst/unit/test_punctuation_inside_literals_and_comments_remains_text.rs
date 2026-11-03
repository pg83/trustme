//@ proc-macro-aux-build: doc_token_observer.rs
use doc_token_observer::discard;

fn main() {
    // Both \ and ` stay inside this comment.
    /* The block comment also contains \ and `. */
    let slash = '\\';
    let tick = '`';
    assert_eq!(slash as u32, 92);
    assert_eq!(tick as u32, 96);
    assert_eq!("\\`", r"\`");
    assert_eq!(b"\\`", &[92, 96]);
    assert_eq!(discard!('\u{1f331}' 1.25f32 "\\`" br"\`" c"\x5c`"), 73);
}
