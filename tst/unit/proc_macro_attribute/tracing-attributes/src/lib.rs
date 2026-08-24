extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn instrument(_attribute: TokenStream, _item: TokenStream) -> TokenStream {
    "fn instrument_was_invoked() -> u32 { 42 }\n\
     fn unicode_char_from_token_stream() -> char { '\\u{2764}' }\n\
     fn raw_integer_from_token_stream() -> u32 { 0_4 }\n\
     fn joint_punctuation_from_token_stream() -> u32 {\n\
         let pair = (0,|value: u32| value);\n\
         (pair.1)(4)\n\
     }"
        .parse()
        .unwrap()
}

#[proc_macro_attribute]
pub fn identity(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    item
}
