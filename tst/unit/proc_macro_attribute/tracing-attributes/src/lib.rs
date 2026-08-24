extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn instrument(_attribute: TokenStream, _item: TokenStream) -> TokenStream {
    "fn instrument_was_invoked() -> u32 { 42 }\n\
     fn unicode_char_from_token_stream() -> char { '\\u{2764}' }\n\
     fn raw_integer_from_token_stream() -> u32 { 0_4 }"
        .parse()
        .unwrap()
}

#[proc_macro_attribute]
pub fn identity(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    item
}
