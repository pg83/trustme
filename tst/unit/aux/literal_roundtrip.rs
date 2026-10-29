extern crate proc_macro;
use proc_macro::{Delimiter, Group, Literal, Punct, Spacing, TokenStream, TokenTree};

#[proc_macro]
pub fn constructed(_: TokenStream) -> TokenStream {
    let literals = [
        Literal::u128_suffixed(u128::MAX),
        Literal::i128_suffixed(i128::MIN),
        Literal::f32_suffixed(-2.5),
        Literal::f64_unsuffixed(0.125),
        Literal::string("a\nλ"),
        Literal::byte_string(&[0, 255, b'"']),
        Literal::character('λ'),
    ];
    let mut values = TokenStream::new();
    for literal in literals {
        values.extend([TokenTree::Literal(literal), Punct::new(',', Spacing::Alone).into()]);
    }
    TokenTree::Group(Group::new(Delimiter::Parenthesis, values)).into()
}

#[proc_macro]
pub fn forwarded(input: TokenStream) -> TokenStream {
    input.clone()
}

#[proc_macro]
pub fn reparsed(input: TokenStream) -> TokenStream {
    input.to_string().parse().unwrap()
}
