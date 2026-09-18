/* The shape `futures_util::select!` hands to `futures_macro::select_internal`: an
   item and locals of the macro's own making wrapped around an expression the
   caller wrote, which names a local of the function the macro was called in
   (`timeout`, in rstest's `execute_with_timeout_async`).

   Every token this macro makes is built at `Span::call_site()`; the caller's own
   tokens are moved across untouched and keep the span - and so the resolution
   context - they arrived with. */
extern crate proc_macro;

use proc_macro::{Delimiter, Group, TokenStream, TokenTree};

fn own_tokens(text: &str) -> TokenStream {
    text.parse().expect("the macro's own tokens lex")
}

fn group(delimiter: Delimiter, stream: TokenStream) -> TokenStream {
    TokenStream::from(TokenTree::Group(Group::new(delimiter, stream)))
}

#[proc_macro]
pub fn pick_only(input: TokenStream) -> TokenStream {
    let mut body = TokenStream::new();
    body.extend(own_tokens("enum Picked<T> { Only(T) } let __picked = Picked::Only"));
    body.extend(group(Delimiter::Parenthesis, input));
    body.extend(own_tokens("; match __picked { Picked::Only(__value) => __value }"));
    group(Delimiter::Brace, body)
}

/* A `let` of the macro's own, and after it the caller's tokens: the macro's name
   is the one in scope for them, exactly as if the macro had been written there. */
#[proc_macro]
pub fn after_a_local_of_its_own(input: TokenStream) -> TokenStream {
    let mut body = TokenStream::new();
    body.extend(own_tokens("let __value = 100u32;"));
    body.extend(input);
    group(Delimiter::Brace, body)
}
