// zerotrie is `#![deny(unsafe_code)]` and derives `yoke::Yokeable`, whose
// expansion has an `unsafe impl` with an `unsafe fn`. Upstream's lints do not
// report code from an external macro (`in_external_macro`: the tokens a proc
// macro makes are in its expansion, defined in another crate); only the
// tokens it passes through from its input are the crate's own.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
#![deny(unsafe_code)]

use proc_macro_item_passthrough::made_unsafe_items;

made_unsafe_items!();

fn main() {
    assert_eq!(call_made(), 3);
}
