// An attribute macro that replaces its item with `fn seven() -> u32`.
extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn make_seven(_attribute: TokenStream, _item: TokenStream) -> TokenStream {
    "fn seven() -> u32 { 7 }".parse().unwrap()
}
