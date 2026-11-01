extern crate proc_macro;
use proc_macro::TokenStream;
use std::fmt::Write;
#[proc_macro]
pub fn growth_tokens(_: TokenStream) -> TokenStream {
    let mut text = String::from("{ macro_rules! early { (seed) => { 19usize }; } (early!(seed), stringify!(");
    for index in 0..390_000 {
        write!(&mut text, " n{:06}", index).unwrap();
    }
    text.push_str("), early!(seed)) }");
    text.parse().unwrap()
}
