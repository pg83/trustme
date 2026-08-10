// Extracted from library/alloc/src/macros.rs:94
#![allow(unused)]
#![allow(unused_must_use)]
extern crate alloc;
fn main() {
    format!("test");                             // => "test"
    format!("hello {}", "world!");               // => "hello world!"
    format!("x = {}, y = {val}", 10, val = 30);  // => "x = 10, y = 30"
    let (x, y) = (1, 2);
    format!("{x} + {y} = 3");                    // => "1 + 2 = 3"
}
