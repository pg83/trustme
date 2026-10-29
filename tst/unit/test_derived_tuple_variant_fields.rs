//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::HelperForward;
#[derive(HelperForward)]
enum Reading { Missing, Pair(#[allow(dead_code)] u8, u16), Named { value: u32 } }
fn value(reading: Reading) -> u32 {
    match reading { Reading::Missing => 0, Reading::Pair(a, b) => a as u32 + b as u32, Reading::Named { value } => value }
}
fn main() {
    assert_eq!(Reading::helper_forward_seen(), 1);
    assert_eq!(value(Reading::Missing), 0);
    assert_eq!(value(Reading::Pair(2, 9)), 11);
    assert_eq!(value(Reading::Named { value: 17 }), 17);
}
