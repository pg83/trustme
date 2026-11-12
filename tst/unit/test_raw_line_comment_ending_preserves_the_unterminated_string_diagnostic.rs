//@ compile-fail: EOF reached in raw string

// A bare CR precedes the CRLF ending.
fn main() {
    let _ = r#"unfinished