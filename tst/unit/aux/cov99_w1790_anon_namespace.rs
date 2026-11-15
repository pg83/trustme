extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn import_anon(_: TokenStream) -> TokenStream {
    let mut output = std::io::stdout().lock();
    for (kind, text) in [
        (2u8, "use"),
        (2, "crate"),
        (1, "::"),
        (2, "#0"),
        (1, "::"),
        (2, "Witness"),
        (1, ";"),
    ] {
        output.write_all(&[kind, text.len() as u8]).unwrap();
        output.write_all(text.as_bytes()).unwrap();
    }
    output.flush().unwrap();
    TokenStream::new()
}
