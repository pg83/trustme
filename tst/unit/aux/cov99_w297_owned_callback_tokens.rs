extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn inspect_drop(body: TokenStream) -> TokenStream {
    let mut output = std::io::stdout();
    for &(kind, text) in &[
        (2, "fn"),
        (2, "inspect_w297"),
        (1, "("),
        (2, "cov99_w297_owned_callback_table"),
        (1, "::"),
        (2, "CallbackMarker#vtable"),
        (1, "{"),
        (2, "#drop_glue"),
        (1, ":"),
        (2, "callback"),
        (1, ","),
        (1, ".."),
        (1, "}"),
        (1, ":"),
        (2, "cov99_w297_owned_callback_table"),
        (1, "::"),
        (2, "CallbackMarker#vtable"),
        (1, ")"),
    ] {
        output.write_all(&[kind, text.len() as u8]).unwrap();
        output.write_all(text.as_bytes()).unwrap();
    }
    output.flush().unwrap();
    body
}
