extern crate proc_macro;
use proc_macro::{Delimiter, Group, Ident, Punct, Spacing, Span, TokenStream, TokenTree};

// What serial_test's `#[serial]` makes of a test: a signature without `->`
// gets `{ run_serial(|| BLOCK); }`, one with `->` gets
// `{ run_serial_returning(|| BLOCK) }`.
#[proc_macro_attribute]
pub fn serial(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let tokens: Vec<TokenTree> = item.into_iter().collect();
    let (block, head) = tokens.split_last().unwrap();
    let returns = head.windows(2).any(|pair| match pair {
        [TokenTree::Punct(minus), TokenTree::Punct(greater)] => minus.as_char() == '-' && greater.as_char() == '>',
        _ => false,
    });
    let runner = if returns { "run_serial_returning" } else { "run_serial" };
    let mut call = TokenStream::new();
    call.extend([
        TokenTree::Ident(Ident::new(runner, Span::call_site())),
        TokenTree::Group(Group::new(Delimiter::Parenthesis, [
            TokenTree::Punct(Punct::new('|', Spacing::Joint)),
            TokenTree::Punct(Punct::new('|', Spacing::Alone)),
            block.clone(),
        ].into_iter().collect())),
    ]);
    if !returns {
        call.extend([TokenTree::Punct(Punct::new(';', Spacing::Alone))]);
    }
    let mut out: TokenStream = head.iter().cloned().collect();
    out.extend([TokenTree::Group(Group::new(Delimiter::Brace, call))]);
    out
}
