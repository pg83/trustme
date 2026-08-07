// Extracted from library/core/src/fmt/mod.rs:1380
#![allow(unused)]
fn main() {
    let x = 42.0; // 42.0 is '4.2E1' in scientific notation

    assert_eq!(format!("{x:E}"), "4.2E1");
}
