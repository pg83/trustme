extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro]
pub fn recover(mut input: TokenStream) -> TokenStream {
    loop {
        match input.into_iter().next().expect("block fragment") {
            TokenTree::Group(group) => {
                if group.delimiter() == Delimiter::Brace {
                    return group.stream();
                }
                input = group.stream();
            }
            _ => panic!("expected group"),
        }
    }
}
