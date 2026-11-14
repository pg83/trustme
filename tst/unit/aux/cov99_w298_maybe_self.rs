extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn seed(_input: TokenStream) -> TokenStream {
    let stdout = std::io::stdout();
    let mut out = stdout.lock();
    // Prefix the normal empty return with #[erase] fn probe(r#self: ()) {}.
    for (kind, text) in [
        (1u8, "#"), (1, "["),
        (2, "cov99_w298_maybe_self"), (1, "::"), (2, "erase"), (1, "]"),
        (2, "fn"), (2, "probe"), (1, "("),
        (2, "r#self"), (1, ":"), (1, "("), (1, ")"), (1, ")"),
        (1, "{"), (1, "}"),
    ] {
        out.write_all(&[kind, text.len() as u8]).unwrap();
        out.write_all(text.as_bytes()).unwrap();
    }
    out.flush().unwrap();
    TokenStream::new()
}

#[proc_macro_attribute]
pub fn erase(_args: TokenStream, input: TokenStream) -> TokenStream {
    let text = input.to_string();
    assert!(text.contains("probe") && text.contains("self"));
    TokenStream::new()
}
