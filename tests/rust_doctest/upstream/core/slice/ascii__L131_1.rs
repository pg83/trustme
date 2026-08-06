// Extracted from library/core/src/slice/ascii.rs:131
#![allow(unused)]
fn main() {
    let s = b"0\t\r\n'\"\\\x9d";
    let escaped = s.escape_ascii().to_string();
    assert_eq!(escaped, "0\\t\\r\\n\\'\\\"\\\\\\x9d");
}
