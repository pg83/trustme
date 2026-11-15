extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn vtable(_: TokenStream) -> TokenStream {
    let source = "cov99_w1284_vtable_trait :: MarkerVtable { drop_field : | p | { let _q = if true { p } else { 0usize as * mut ( ) } ; } , size_field : 0usize , align_field : 1usize }";
    let mut bytes = Vec::new();
    for token in source.split_whitespace() {
        let name = match token {
            "MarkerVtable" => "Marker#vtable",
            "drop_field" => "#drop_glue",
            "size_field" => "#size",
            "align_field" => "#align",
            other => other,
        };
        let tag = if token.as_bytes()[0].is_ascii_digit() {
            12
        } else if token.as_bytes()[0].is_ascii_alphabetic() || token.starts_with('_') {
            2
        } else {
            1
        };
        assert!(name.len() < 128);
        bytes.push(tag);
        bytes.push(name.len() as u8);
        bytes.extend_from_slice(name.as_bytes());
    }
    std::io::stdout().write_all(&bytes).unwrap();
    std::io::stdout().flush().unwrap();
    TokenStream::new()
}
