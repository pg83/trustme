// An attribute macro that replaces its item with `const ITEM: &str`, the
// text of the item it was given.
extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn item_text(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    format!("const ITEM: &str = {:?};", item.to_string()).parse().unwrap()
}

// A derive that adds `const DERIVE_INPUT: &str`, the text of its input, and
// an attribute macro that puts the derive on its item.
#[proc_macro_derive(InputText, attributes(note))]
pub fn input_text(item: TokenStream) -> TokenStream {
    format!("const DERIVE_INPUT: &str = {:?};", item.to_string()).parse().unwrap()
}

#[proc_macro_attribute]
pub fn with_input_text(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    let mut output: TokenStream = "#[derive(item_text::InputText)]".parse().unwrap();
    output.extend(item);
    output
}
