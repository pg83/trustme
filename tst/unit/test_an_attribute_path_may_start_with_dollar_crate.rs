//@ proc-macro-aux-build: item_head_text.rs
//@ aux-build: dollar_crate_attr.rs
// tor-hsclient (arti) uses inventory's `submit!`, whose `__do_submit!` writes
// `#[cfg_attr(target_family = "wasm", $crate::__private::attr(..))]`. An
// attribute's path is an ordinary path upstream, `$crate` included; ours took
// only identifiers and stopped at the crate of `$crate`: "Unexpected token
// TOK_STRING, expected TOK_IDENT".
use dollar_crate_attr::headed;

headed! {
    pub fn launch() {}
}

fn main() {
    assert_eq!(ITEM_HEAD, "pub fn launch");
}
