//@ aux-build: dollar_crate_body.rs
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// rocket's `impl_strict_from_data_from_capped!` puts `#[crate::async_trait]`
// on an impl whose body names `<$crate::data::Capped<String> as FromData>`.
// Upstream hands the attribute the item's collected tokens, where `$crate` is
// one identifier that keeps naming the macro's crate. The body reached the
// proc macro re-printed as text, the named crate came out as the two tokens
// `$` `crate`, and the item it gave back did not parse.
extern crate dollar_crate_body;
extern crate proc_macro_item_passthrough;

dollar_crate_body::attributed!(proc_macro_item_passthrough::echo_item, twelve);

fn main() {
    assert_eq!(twelve(), 12);
}
