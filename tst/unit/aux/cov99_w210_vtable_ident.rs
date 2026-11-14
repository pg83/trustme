extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn owned_vtable(_: TokenStream) -> TokenStream {
    let mut output = std::io::stdout().lock();
    for &(kind, text) in &[
        (2, "cov99_w210_vtable_seed"), (1, "::"), (2, "Seed#vtable"),
        (1, "{"), (2, "#drop_glue"), (1, ":"),
        (1, "|"), (2, "p"), (1, "|"),
        (2, "p"), (1, "."), (2, "w210_probe"), (1, "("), (1, ")"),
        (1, ","), (2, "#size"), (1, ":"), (12, "0usize"),
        (1, ","), (2, "#align"), (1, ":"), (12, "1usize"), (1, "}"),
    ] {
        output.write_all(&[kind, text.len() as u8]).unwrap();
        output.write_all(text.as_bytes()).unwrap();
    }
    output.flush().unwrap();
    TokenStream::new()
}
