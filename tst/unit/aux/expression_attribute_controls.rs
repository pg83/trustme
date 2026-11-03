extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn identity(_: TokenStream, input: TokenStream) -> TokenStream { input }

#[proc_macro_attribute]
pub fn twice(_: TokenStream, input: TokenStream) -> TokenStream {
    format!("({input}) * 2u32").parse().unwrap()
}

#[proc_macro_attribute]
pub fn increment(_: TokenStream, input: TokenStream) -> TokenStream {
    format!("({input}) + 1u32").parse().unwrap()
}

#[proc_macro_attribute]
pub fn generate(_: TokenStream, _: TokenStream) -> TokenStream {
    "#[expression_attribute_controls::twice] 23u32".parse().unwrap()
}

#[proc_macro_attribute]
pub fn local(_: TokenStream, _: TokenStream) -> TokenStream {
    "selected!()".parse().unwrap()
}

#[proc_macro_attribute]
pub fn erase(_: TokenStream, _: TokenStream) -> TokenStream {
    "17u32".parse().unwrap()
}

#[proc_macro_attribute]
pub fn empty(_: TokenStream, _: TokenStream) -> TokenStream { TokenStream::new() }

#[proc_macro_attribute]
pub fn trailing(_: TokenStream, _: TokenStream) -> TokenStream {
    "1u32 2u32".parse().unwrap()
}

#[proc_macro_attribute]
pub fn broken(_: TokenStream, _: TokenStream) -> TokenStream {
    panic!("expression attribute control panic")
}
