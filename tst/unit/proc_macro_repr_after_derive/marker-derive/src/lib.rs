// A derive that adds nothing: the item it is applied to must keep every other attribute.
extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_derive(Marker)]
pub fn marker(_input: TokenStream) -> TokenStream {
    TokenStream::new()
}
