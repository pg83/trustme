// time-macros reports a bad component with `span.start()` and `span.end()`
// and uses `line()`, `column()` and `file()` on the result. These are stable
// since 1.88: `start()` and `end()` are empty spans directly before and after
// a span, `line()` and `column()` are one-indexed and describe where the
// span starts, `file()` is the source path for display.
extern crate proc_macro;

use proc_macro::{Span, TokenStream};

fn tuple(values: &[usize]) -> TokenStream {
    let items: Vec<String> = values.iter().map(|v| v.to_string()).collect();
    format!("({})", items.join(", ")).parse().expect("a tuple lexes")
}

#[proc_macro]
pub fn call_site_location(_input: TokenStream) -> TokenStream {
    let span = Span::call_site();
    format!("({}, {}, {:?})", span.line(), span.column(), span.file())
        .parse()
        .expect("a tuple lexes")
}

#[proc_macro]
pub fn first_token_location(input: TokenStream) -> TokenStream {
    let token = input.into_iter().next().expect("a token");
    let span = token.span();
    let (start, end) = (span.start(), span.end());
    tuple(&[
        span.line(),
        span.column(),
        start.line(),
        start.column(),
        end.line(),
        end.column(),
    ])
}
