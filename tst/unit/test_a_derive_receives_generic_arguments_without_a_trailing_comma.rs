//@ proc-macro-aux-build: field_tokens_derive.rs
// tor-chanmgr (arti) has `outbound_proxy: Option<ProxyProtocol>` in a struct
// that derives Deftly with tor-config's `TorConfig` template, which hands the
// field type to `normalize_and_invoke!`, whose arm `Option $(::)? < $t:ty >`
// picks the `Option` handling. Upstream gives a derive the item's tokens as
// written; we wrote a type's generic arguments with a comma after each one,
// `Option<ProxyProtocol,>`, the `Option` arm did not match, and the catch-all
// asserted the type was no special case.
use field_tokens_derive::FieldTokens;
use std::collections::HashMap;

#[derive(FieldTokens)]
#[allow(dead_code)]
struct Config {
    proxy: Option<u8>,
    table: HashMap<u8, Vec<u16>>,
}

fn main() {
    assert_eq!(Config::FIELD_TOKENS, "proxy : Option < u8 > , table : HashMap < u8 , Vec < u16 > > ,");
}
