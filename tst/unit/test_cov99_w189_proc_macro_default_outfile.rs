//@ compile-only
//@ compile-flags: --crate-type proc-macro -o '' --out-dir $OUT --emit=metadata -Cpanic=abort

#![crate_type = "proc-macro"]

extern crate proc_macro;

#[proc_macro]
pub fn identity(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    input
}
