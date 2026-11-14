extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn synthetic_choice(_: TokenStream) -> TokenStream {
    let mut output = std::io::stdout();
    for (class, value) in [
        (2u8, "enum"),
        (2, "closure#C_2"),
        (1, "{"),
        (2, "Only"),
        (1, "}"),
        (2, "type"),
        (2, "Choice"),
        (1, "="),
        (2, "closure#C_2"),
        (1, ";"),
        (1, ""),
    ] {
        output.write_all(&[class, value.len() as u8]).unwrap();
        output.write_all(value.as_bytes()).unwrap();
    }
    output.flush().unwrap();
    "enum NativeChoice { Only } type Choice = NativeChoice;".parse().unwrap()
}
