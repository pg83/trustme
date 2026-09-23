//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item_spelled;

#[inline]
#[echo_item_spelled("inline")]
fn one() -> u8 {
    1
}

#[allow(dead_code)]
#[echo_item_spelled("dead_code")]
#[echo_item_spelled("must_use")]
#[must_use]
fn two() -> u8 {
    2
}

fn main() {
    assert_eq!(one() + two(), 3);
}
