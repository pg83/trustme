extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro_attribute]
pub fn strips_test_attributes(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let mut out = Vec::new();
    let mut tokens = item.into_iter().peekable();
    while let Some(token) = tokens.next() {
        if let TokenTree::Punct(punct) = &token {
            if punct.as_char() == '#' {
                if let Some(TokenTree::Group(group)) = tokens.peek() {
                    if group.delimiter() == Delimiter::Bracket {
                        if let Some(TokenTree::Ident(name)) = group.stream().into_iter().next() {
                            let name = name.to_string();
                            if name == "should_panic" || name == "ignore" {
                                tokens.next();
                                continue;
                            }
                        }
                    }
                }
            }
        }
        out.push(token);
    }
    out.into_iter().collect()
}
