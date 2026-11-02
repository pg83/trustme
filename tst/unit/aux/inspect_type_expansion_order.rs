extern crate proc_macro;
use proc_macro::{Delimiter, TokenStream, TokenTree};

fn contains_name(input: TokenStream, name: &str) -> bool {
    input.into_iter().any(|token| match token {
        TokenTree::Ident(ident) => ident.to_string() == name,
        TokenTree::Group(group) => contains_name(group.stream(), name),
        _ => false,
    })
}

fn inferred_arrays(input: TokenStream) -> usize {
    input.into_iter().map(|token| match token {
        TokenTree::Group(group) => {
            let direct = group.delimiter() == Delimiter::Bracket && contains_name(group.stream(), "_");
            direct as usize + inferred_arrays(group.stream())
        }
        _ => 0,
    }).sum()
}

fn function_name(input: TokenStream) -> String {
    let mut found = false;
    for token in input {
        if let TokenTree::Ident(ident) = token {
            if found { return ident.to_string(); }
            found = ident.to_string() == "fn";
        }
    }
    panic!("function expected")
}

#[proc_macro_attribute]
pub fn replace_inferred_arrays(attr: TokenStream, input: TokenStream) -> TokenStream {
    format!("const {}: usize = {};", attr, inferred_arrays(input)).parse().unwrap()
}

#[proc_macro_derive(InspectTypes)]
pub fn inspect_types(input: TokenStream) -> TokenStream {
    format!("const OBSERVED_SELF: bool = {}; const OBSERVED_PATTERN: bool = {};",
        contains_name(input.clone(), "Self"), contains_name(input, "pattern_type")).parse().unwrap()
}

#[proc_macro_attribute]
pub fn erase_nested_block(_: TokenStream, input: TokenStream) -> TokenStream {
    format!("fn {}() -> bool {{ true }}", function_name(input)).parse().unwrap()
}

#[proc_macro_attribute]
pub fn observe_nested_pattern(_: TokenStream, input: TokenStream) -> TokenStream {
    format!("fn {}() -> bool {{ {} }}", function_name(input.clone()),
        contains_name(input, "pattern_type")).parse().unwrap()
}
