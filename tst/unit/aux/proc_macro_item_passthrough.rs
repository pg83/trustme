// Macro hosts that hand their input back unchanged, so a unit can pin down what
// the compiler wrote into a macro's token stream rather than what the macro did
// with it.
extern crate proc_macro;

use proc_macro::TokenStream;

// Function-like: the invocation body comes back as the expansion.
#[proc_macro]
pub fn echo(input: TokenStream) -> TokenStream {
    input
}

// Attribute: the annotated item comes back unchanged.
#[proc_macro_attribute]
pub fn echo_item(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    item
}

// The same, but first checking how the item was spelled: the attribute takes a
// string literal that the item's tokens have to contain.
#[proc_macro_attribute]
pub fn echo_item_spelled(attribute: TokenStream, item: TokenStream) -> TokenStream {
    let quoted = attribute.to_string();
    let expected = quoted.trim().trim_matches('"');
    let text = item.to_string();
    assert!(
        text.contains(expected),
        "the item's tokens should contain `{}`, they are: {}",
        expected,
        text
    );
    item
}

// Attribute: the item comes back unchanged, after checking that its tokens hold
// no parentheses around a type that a source without them would not need: a
// group right after `&`, `&mut`, `*const` or `*mut`, or one opening with `dyn`,
// is only there for a type of several bounds (`&(dyn A + Send)`).
#[proc_macro_attribute]
pub fn echo_item_without_added_type_parens(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    use proc_macro::{Delimiter, TokenTree};

    fn is_punct(token: Option<&TokenTree>, c: char) -> bool {
        matches!(token, Some(TokenTree::Punct(p)) if p.as_char() == c)
    }

    fn is_ident(token: Option<&TokenTree>, name: &str) -> bool {
        matches!(token, Some(TokenTree::Ident(i)) if i.to_string() == name)
    }

    fn walk(stream: TokenStream) {
        let tokens: Vec<TokenTree> = stream.into_iter().collect();
        for (i, token) in tokens.iter().enumerate() {
            let TokenTree::Group(group) = token else { continue };
            if group.delimiter() == Delimiter::Parenthesis {
                let inner: Vec<TokenTree> = group.stream().into_iter().collect();
                let previous = if i > 0 { tokens.get(i - 1) } else { None };
                let before = if i > 1 { tokens.get(i - 2) } else { None };
                let after_pointer = is_punct(previous, '&')
                    || ((is_ident(previous, "mut") || is_ident(previous, "const"))
                        && (is_punct(before, '&') || is_punct(before, '*')));
                let opens_dyn = is_ident(inner.first(), "dyn");
                let several_bounds = inner.iter().any(|t| is_punct(Some(t), '+'));
                assert!(
                    !(after_pointer || opens_dyn) || several_bounds,
                    "parentheses the source did not need: {}",
                    group
                );
            }
            walk(group.stream());
        }
    }

    walk(item.clone());
    item
}

// Function-like: `raw_member!(value)` is `value.r#fn`, the member written
// with `Ident::new_raw`, and `raw_name!(r#fn)` is the name the compiler
// handed over, as a string literal.
#[proc_macro]
pub fn raw_member(input: TokenStream) -> TokenStream {
    use proc_macro::{Ident, Punct, Spacing, Span, TokenTree};
    let mut output: Vec<TokenTree> = input.into_iter().collect();
    output.push(Punct::new('.', Spacing::Alone).into());
    output.push(Ident::new_raw("fn", Span::call_site()).into());
    output.into_iter().collect()
}

#[proc_macro]
pub fn raw_name(input: TokenStream) -> TokenStream {
    use proc_macro::{Literal, TokenTree};
    let name = input.into_iter().next().unwrap().to_string();
    TokenTree::from(Literal::string(&name)).into()
}

// Function-like: the input's top-level tokens, each as the compiler handed it
// over, joined by spaces into a string literal.
#[proc_macro]
pub fn token_texts(input: TokenStream) -> TokenStream {
    use proc_macro::{Literal, TokenTree};
    let texts: Vec<String> = input.into_iter().map(|token| token.to_string()).collect();
    TokenTree::from(Literal::string(&texts.join(" "))).into()
}

// A literal parsed from its text, the text given as a string literal:
// `literal_from_text!("-5")` is `"-5".parse::<Literal>()`.
#[proc_macro]
pub fn literal_from_text(input: TokenStream) -> TokenStream {
    use proc_macro::{Literal, TokenTree};
    let quoted = input.to_string();
    let text = quoted.trim().trim_matches('"');
    let literal: Literal = text.parse().unwrap();
    assert!("- 5".parse::<Literal>().is_err());
    assert!("-\"s\"".parse::<Literal>().is_err());
    TokenTree::from(literal).into()
}

// Items the macro writes itself, `unsafe` among them.
#[proc_macro]
pub fn made_unsafe_items(_input: TokenStream) -> TokenStream {
    "unsafe fn made_unsafe() -> u8 { 3 } fn call_made() -> u8 { unsafe { made_unsafe() } }".parse().unwrap()
}

// An attribute named `test` in the manner of `tokio::test`: the annotated
// async fn comes back as a plain `#[test]` function of the same name, its
// body dropped.
#[proc_macro_attribute]
pub fn test(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    use proc_macro::TokenTree;
    let mut after_fn = false;
    let mut name = None;
    for token in item {
        if let TokenTree::Ident(ident) = &token {
            if after_fn {
                name = Some(ident.to_string());
                break;
            }
            after_fn = ident.to_string() == "fn";
        }
    }
    format!("#[::core::prelude::v1::test] fn {}() {{}}", name.unwrap()).parse().unwrap()
}

// Source text parsed back into tokens, as pest_derive does through
// `quote`: the expansion is the character literal `'\x41'`.
#[proc_macro]
pub fn hex_escaped_char(_input: TokenStream) -> TokenStream {
    "'\\x41'".parse().unwrap()
}
