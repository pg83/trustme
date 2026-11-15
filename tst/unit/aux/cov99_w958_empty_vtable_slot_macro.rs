extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn empty_slot(_: TokenStream) -> TokenStream {
    let mut out = std::io::stdout().lock();
    for (tag, text) in [
        (2u8, "struct"), (2, "Slot#vtable"), (1, "{"),
        (2, "drop"), (1, ":"), (2, "usize"), (1, ","),
        (2, "size"), (1, ":"), (2, "usize"), (1, ","),
        (2, "align"), (1, ":"), (2, "usize"), (1, ","),
        (2, "get"), (1, ":"), (2, "fn"), (1, "("), (1, ")"),
        (1, "}"),
    ] {
        out.write_all(&[tag, text.len() as u8]).unwrap();
        out.write_all(text.as_bytes()).unwrap();
    }
    out.write_all(&[1, 0]).unwrap();
    out.flush().unwrap();
    TokenStream::new()
}
