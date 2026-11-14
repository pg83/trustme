extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro]
pub fn recover(mut input: TokenStream) -> TokenStream {
    loop {
        let first = input.into_iter().next().expect("block fragment");
        match first {
            TokenTree::Group(group) => {
                if group.delimiter() == Delimiter::Brace {
                    let mut body: Vec<TokenTree> = group.stream().into_iter().collect();
                    if matches!(body.last(), Some(TokenTree::Punct(p)) if p.as_char() == ';') {
                        body.pop();
                    }
                    return body.into_iter().collect();
                }
                input = group.stream();
            }
            _ => panic!("expected group"),
        }
    }
}
