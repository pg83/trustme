//@ aux-build: own_root_external_macro.rs
#![feature(decl_macro, rustc_attrs)]

extern crate own_root_external_macro;

fn main() {
    own_root_external_macro::verify_rule_capture!();
}
