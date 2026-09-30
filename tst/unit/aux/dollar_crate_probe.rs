extern crate proc_macro;
use proc_macro::{TokenStream, TokenTree};

#[proc_macro]
pub fn first_token(input: TokenStream) -> TokenStream {
    let text = match input.into_iter().next() {
        Some(TokenTree::Ident(ident)) => format!("ident:{}", ident),
        Some(TokenTree::Punct(punct)) => format!("punct:{}", punct.as_char()),
        Some(TokenTree::Group(_)) => "group".to_string(),
        Some(TokenTree::Literal(literal)) => format!("literal:{}", literal),
        None => "none".to_string(),
    };
    format!("{:?}", text).parse().unwrap()
}

#[proc_macro]
pub fn echo(input: TokenStream) -> TokenStream {
    input
}
