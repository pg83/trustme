extern crate proc_macro;
use proc_macro::{Delimiter, TokenStream, TokenTree};

// `struct Name { .. }` gets `impl Name { pub const FIELD_TOKENS: &str = ".."; }`
// with the tokens between its braces, one space apart.
#[proc_macro_derive(FieldTokens)]
pub fn field_tokens(item: TokenStream) -> TokenStream {
    let mut name = None;
    let mut after_struct = false;
    let mut fields = Vec::new();
    for tt in item {
        match tt {
            TokenTree::Ident(ident) if after_struct && name.is_none() => name = Some(ident.to_string()),
            TokenTree::Ident(ident) => after_struct = ident.to_string() == "struct",
            TokenTree::Group(group) if group.delimiter() == Delimiter::Brace => {
                flatten(group.stream(), &mut fields);
            }
            _ => {}
        }
    }
    format!("impl {} {{ pub const FIELD_TOKENS: &str = {:?}; }}", name.unwrap(), fields.join(" ")).parse().unwrap()
}

fn flatten(stream: TokenStream, out: &mut Vec<String>) {
    for tt in stream {
        match tt {
            TokenTree::Group(group) => {
                let (open, close) = match group.delimiter() {
                    Delimiter::Parenthesis => ("(", ")"),
                    Delimiter::Bracket => ("[", "]"),
                    Delimiter::Brace => ("{", "}"),
                    Delimiter::None => ("", ""),
                };
                out.push(open.to_string());
                flatten(group.stream(), out);
                out.push(close.to_string());
            }
            other => out.push(other.to_string()),
        }
    }
}
