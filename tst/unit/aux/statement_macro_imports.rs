extern crate proc_macro;
use proc_macro::TokenStream;
#[proc_macro]
pub fn make_value(_: TokenStream) -> TokenStream {
    "let generated: u8 = 17;".parse().unwrap()
}
