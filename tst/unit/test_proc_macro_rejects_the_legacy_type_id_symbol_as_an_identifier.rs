//@ proc-macro-aux-build: reject_legacy_type_id_ident.rs
//@ compile-fail: error
extern crate reject_legacy_type_id_ident;
reject_legacy_type_id_ident::reject_legacy_name!();
fn main() {}
