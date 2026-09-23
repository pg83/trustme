//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item_spelled;

#[echo_item_spelled("{ 1 }")]
fn one() -> u8 {
    1
}

#[echo_item_spelled("0x10 + 2")]
fn eighteen() -> u32 {
    0x10 + 2
}

fn main() {
    assert_eq!(one(), 1);
    assert_eq!(eighteen(), 18);
}
