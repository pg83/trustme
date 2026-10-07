extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn item_head(_: TokenStream, item: TokenStream) -> TokenStream {
    let head: Vec<String> = item.into_iter().take(3).map(|token| token.to_string()).collect();
    format!("pub const ITEM_HEAD: &str = {:?};", head.join(" ")).parse().unwrap()
}
