// Extracted from library/core/src/option.rs:932
#![allow(unused)]
fn main() {
    let x: Option<&str> = None;
    x.expect("fruits are healthy"); // panics with `fruits are healthy`
}
