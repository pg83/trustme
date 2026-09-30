// An attribute that hands back the impl it is on after checking that no
// method's `self` is followed by `:` - that is, that every receiver reached
// the macro written as its shorthand.
extern crate proc_macro;

use proc_macro::{TokenStream, TokenTree};

fn check(stream: TokenStream) {
    let tokens: Vec<TokenTree> = stream.into_iter().collect();
    for (i, token) in tokens.iter().enumerate() {
        match token {
            TokenTree::Ident(ident) if ident.to_string() == "self" => {
                if let Some(TokenTree::Punct(p)) = tokens.get(i + 1) {
                    assert!(p.as_char() != ':', "a receiver reached the macro as `self: Type`");
                }
            }
            TokenTree::Group(group) => check(group.stream()),
            _ => {}
        }
    }
}

#[proc_macro_attribute]
pub fn receivers_are_shorthand(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    check(item.clone());
    item
}
