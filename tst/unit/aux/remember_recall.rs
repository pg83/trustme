extern crate proc_macro;
use proc_macro::TokenStream;
use std::sync::Mutex;

static REMEMBERED: Mutex<String> = Mutex::new(String::new());

#[proc_macro_attribute]
pub fn remember(_: TokenStream, item: TokenStream) -> TokenStream {
    *REMEMBERED.lock().unwrap() = item.to_string();
    item
}

#[proc_macro]
pub fn recall(_: TokenStream) -> TokenStream {
    format!("{:?}", REMEMBERED.lock().unwrap().clone()).parse().unwrap()
}
