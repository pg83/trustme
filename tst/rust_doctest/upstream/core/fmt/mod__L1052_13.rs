// Extracted from library/core/src/fmt/mod.rs:1052
#![allow(unused)]
fn main() {
    let x = 42; // 42 is '52' in octal

    assert_eq!(format!("{x:o}"), "52");
    assert_eq!(format!("{x:#o}"), "0o52");

    assert_eq!(format!("{:o}", -16), "37777777760");
}
