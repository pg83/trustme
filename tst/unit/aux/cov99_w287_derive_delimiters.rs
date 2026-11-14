extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_derive(DelimiterValue, attributes(w287_helper,),)]
pub fn delimiter_value(_input: TokenStream) -> TokenStream {
    "impl DelimiterProbe { fn value() -> u32 { 287 } }".parse().unwrap()
}
