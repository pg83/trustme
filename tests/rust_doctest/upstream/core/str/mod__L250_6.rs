// Extracted from library/core/src/str/mod.rs:250
#![allow(unused)]
fn main() {
    // "Hello, Rust!" as a mutable vector
    let mut hellorust = vec![72, 101, 108, 108, 111, 44, 32, 82, 117, 115, 116, 33];

    // As we know these bytes are valid, we can use `unwrap()`
    let outstr = str::from_utf8_mut(&mut hellorust).unwrap();

    assert_eq!("Hello, Rust!", outstr);
}
