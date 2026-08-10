// Extracted from library/core/src/option.rs:1024
#![allow(unused)]
fn main() {
    assert_eq!(Some("car").unwrap_or("bike"), "car");
    assert_eq!(None.unwrap_or("bike"), "bike");
}
