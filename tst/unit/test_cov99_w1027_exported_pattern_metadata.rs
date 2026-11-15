//@ aux-build: cov99_w1027_exported_pattern.rs

use cov99_w1027_exported_pattern::exported_pattern;

fn main() {
    assert_eq!(exported_pattern!(), 11u32);
    assert_eq!(exported_pattern!(23u32), 30u32);
}
