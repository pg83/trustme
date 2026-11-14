extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn emit(_: TokenStream) -> TokenStream {
    // Send an item whose numeric field is transported as Ident, not Literal.
    let mut out = std::io::stdout();
    for &(kind, text) in &[
        (1u8, "#"),
        (1, "["),
        (2, "cov99_w299_numeric_ident_pattern"),
        (1, "::"),
        (2, "pass"),
        (1, "]"),
        (2, "fn"),
        (2, "probe"),
        (1, "("),
        (2, "Name"),
        (1, "{"),
        (2, "0"),
        (1, ":"),
        (2, "_"),
        (1, "}"),
        (1, ":"),
        (2, "Name"),
        (1, ")"),
        (1, "{"),
        (1, "}"),
    ] {
        out.write_all(&[kind, text.len() as u8]).unwrap();
        out.write_all(text.as_bytes()).unwrap();
    }
    out.flush().unwrap();
    TokenStream::new()
}

#[proc_macro_attribute]
pub fn pass(_: TokenStream, item: TokenStream) -> TokenStream {
    item
}
