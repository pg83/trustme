//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// generic-array puts `#[rustversion::since(1.83)]` on a `const unsafe fn`.
// An attribute macro gets the item as its tokens, and the qualifiers come in
// the order the grammar has them - `const`, `async`, `unsafe`, `extern "abi"`
// (rustc's `print_fn_header_info`); `unsafe const fn` does not parse back.
use proc_macro_item_passthrough::echo_item;

#[echo_item]
const unsafe fn both(v: u8) -> u8 {
    v + 1
}

#[echo_item]
async unsafe fn later() -> u8 {
    2
}

#[echo_item]
const unsafe extern "C" fn all_three(v: u8) -> u8 {
    v + 3
}

fn main() {
    unsafe {
        let _ = later();
        assert_eq!(both(1), 2);
        assert_eq!(all_three(1), 4);
    }
}
