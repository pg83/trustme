extern crate proc_macro;

use proc_macro::{Delimiter, Group, TokenStream, TokenTree};

#[proc_macro]
pub fn assert_fragment(input: TokenStream) -> TokenStream {
    let mut nested = input;
    loop {
        match nested.into_iter().next().expect("expression group") {
            TokenTree::Group(group) => {
                let delimiter = group.delimiter();
                let contents = group.stream();
                if delimiter == Delimiter::Parenthesis {
                    let mut output: TokenStream = "assert!".parse().unwrap();
                    output.extend([TokenTree::Group(Group::new(Delimiter::Parenthesis, contents))]);
                    return output;
                }
                assert!(delimiter == Delimiter::None);
                nested = contents;
            }
            _ => panic!("expected a grouped expression"),
        }
    }
}
