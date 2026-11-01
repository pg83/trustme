extern crate proc_macro;
use proc_macro::TokenStream;
#[proc_macro_attribute]
pub fn pass(_: TokenStream, item: TokenStream) -> TokenStream { item }
#[proc_macro_attribute]
pub fn require_source(_: TokenStream, item: TokenStream) -> TokenStream {
    let text = item.to_string();
    assert!(text.contains("derive"), "derive tokens disappeared");
    assert!(!text.contains("impl"), "generated impl leaked into macro input");
    item
}
