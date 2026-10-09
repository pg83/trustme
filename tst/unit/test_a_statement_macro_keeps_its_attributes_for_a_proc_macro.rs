//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// multiversion 0.9: an item inside a function body goes to an attribute
// macro with `#[cfg(..)] assert!(selected_target!()..)` among its
// statements. The statement's attributes are part of its tokens (rustc's
// print_stmt prints them for a macro statement too), so the false cfg
// strips the statement after the macro hands the item back.
use proc_macro_item_passthrough::echo_item;

fn main() {
    #[echo_item]
    fn inner() -> u32 {
        #[cfg(any())]
        missing_macro!();
        #[cfg(any())]
        assert!(missing_macro!().contains(1));
        7
    }

    assert_eq!(inner(), 7);
}
