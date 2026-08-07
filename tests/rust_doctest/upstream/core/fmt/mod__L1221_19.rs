// Extracted from library/core/src/fmt/mod.rs:1221
#![allow(unused)]
fn main() {
    let y = 42; // 42 is '2A' in hex

    assert_eq!(format!("{y:X}"), "2A");
    assert_eq!(format!("{y:#X}"), "0x2A");

    assert_eq!(format!("{:X}", -16), "FFFFFFF0");
}
