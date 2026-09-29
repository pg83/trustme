// serde_derive's `#[serde(default = "path")]`: the path is parsed out of the
// string literal and every token of it takes the literal's span, and the
// `let __default` binding it initialises is built at that span too
// (`quote_spanned!(path.span()=> ...)`), while the field read from it,
// `__default.field`, is built at `Span::call_site()`.
extern crate proc_macro;

use proc_macro::{Delimiter, Group, Literal, Span, TokenStream, TokenTree};

fn respan(stream: TokenStream, span: Span) -> TokenStream {
    stream
        .into_iter()
        .map(|token| match token {
            TokenTree::Group(group) => {
                let mut inner = Group::new(group.delimiter(), respan(group.stream(), span));
                inner.set_span(span);
                TokenTree::Group(inner)
            }
            mut other => {
                other.set_span(span);
                other
            }
        })
        .collect()
}

fn own_tokens(text: &str) -> TokenStream {
    text.parse().expect("the macro's own tokens lex")
}

fn group(delimiter: Delimiter, stream: TokenStream) -> TokenStream {
    TokenStream::from(TokenTree::Group(Group::new(delimiter, stream)))
}

fn made_from(literal: Literal, tokens: &[TokenTree]) -> TokenStream {
    let mut name = None;
    let mut field = None;
    for (i, token) in tokens.iter().enumerate() {
        match token {
            TokenTree::Ident(keyword) if keyword.to_string() == "struct" => {
                name = Some(tokens[i + 1].to_string());
            }
            TokenTree::Group(g) if g.delimiter() == Delimiter::Brace => {
                field = g.stream().into_iter().next().map(|t| t.to_string());
            }
            _ => {}
        }
    }
    let name = name.expect("a struct");
    let field = field.expect("a named field");
    let text = literal.to_string();
    let callee = own_tokens(text.trim_matches('"'));
    let mut binding = own_tokens(&format!("let __default: {} =", name));
    binding.extend(callee);
    binding.extend(own_tokens("();"));
    let mut body = respan(binding, literal.span());
    body.extend(own_tokens(&format!("__default.{}", field)));
    let mut item = own_tokens("pub fn made() -> u32");
    item.extend(group(Delimiter::Brace, body));
    let mut out = own_tokens(&format!("impl {}", name));
    out.extend(group(Delimiter::Brace, item));
    out
}

#[proc_macro_derive(DefaultFrom, attributes(default_from))]
pub fn default_from(input: TokenStream) -> TokenStream {
    let tokens: Vec<TokenTree> = input.into_iter().collect();
    let mut path = None;
    for token in &tokens {
        if let TokenTree::Group(g) = token {
            let inner: Vec<TokenTree> = g.stream().into_iter().collect();
            if let [TokenTree::Ident(attr), TokenTree::Punct(_), TokenTree::Literal(lit)] = &inner[..] {
                if attr.to_string() == "default_from" {
                    path = Some(lit.clone());
                }
            }
        }
    }
    made_from(path.expect("#[default_from = \"path\"]"), &tokens)
}

// The same as an attribute: `#[default_from_attr("path")]`.
#[proc_macro_attribute]
pub fn default_from_attr(attribute: TokenStream, item: TokenStream) -> TokenStream {
    let literal = match attribute.into_iter().next() {
        Some(TokenTree::Literal(literal)) => literal,
        _ => panic!("#[default_from_attr(\"path\")]"),
    };
    let tokens: Vec<TokenTree> = item.clone().into_iter().collect();
    let mut out = item;
    out.extend(made_from(literal, &tokens));
    out
}
