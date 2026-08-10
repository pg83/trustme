// Extracted from library/alloc/src/fmt.rs:82
#![allow(unused)]
#![allow(unused_must_use)]
extern crate alloc;
fn main() {
    format!("{argument}", argument = "test");   // => "test"
    format!("{name} {}", 1, name = 2);          // => "2 1"
    format!("{a} {c} {b}", a="a", b='b', c=3);  // => "a 3 b"
}
