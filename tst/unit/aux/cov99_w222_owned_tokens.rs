extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn vtable(_: TokenStream) -> TokenStream {
    let tokens = [
        (2u8, "cov99_w222_owned_seed"), (1, "::"), (2, "Seed#vtable"),
        (1, "{"), (2, "#drop_glue"), (1, ":"),
        (1, "|"), (2, "p"), (1, "|"),
        (2, "constrain"), (1, "("), (2, "p"), (1, ")"), (1, ","),
        (2, "#size"), (1, ":"), (12, "0usize"), (1, ","),
        (2, "#align"), (1, ":"), (12, "1usize"), (1, "}"),
    ];
    let mut reply = Vec::new();
    for (kind, token) in tokens {
        reply.push(kind);
        reply.push(token.len() as u8);
        reply.extend_from_slice(token.as_bytes());
    }
    let mut stdout = std::io::stdout();
    stdout.write_all(&reply).unwrap();
    stdout.flush().unwrap();
    TokenStream::new()
}
