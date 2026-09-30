extern crate proc_macro;
use proc_macro::{Delimiter, Group, TokenStream, TokenTree};

// `struct Name(pub T);` gets `impl Name { pub fn get(&self) -> &T { &self.0 } }`,
// with `T` as the tokens it arrived as, up to a trailing comma.
#[proc_macro_derive(FieldGetter)]
pub fn field_getter(item: TokenStream) -> TokenStream {
    let mut name = None;
    let mut field = None;
    let mut after_struct = false;
    for tt in item {
        match tt {
            TokenTree::Ident(ident) if after_struct && name.is_none() => name = Some(ident.to_string()),
            TokenTree::Ident(ident) => after_struct = ident.to_string() == "struct",
            TokenTree::Group(group) if group.delimiter() == Delimiter::Parenthesis => field = Some(group.stream()),
            _ => {}
        }
    }
    let field_type: TokenStream = field
        .unwrap()
        .into_iter()
        .skip(1)
        .take_while(|tt| !matches!(tt, TokenTree::Punct(p) if p.as_char() == ','))
        .collect();
    let body: TokenStream = "pub fn get(&self) -> &"
        .parse::<TokenStream>()
        .unwrap()
        .into_iter()
        .chain(field_type)
        .chain("{ &self.0 }".parse::<TokenStream>().unwrap())
        .collect();
    let mut out: TokenStream = format!("impl {}", name.unwrap()).parse().unwrap();
    out.extend([TokenTree::Group(Group::new(Delimiter::Brace, body))]);
    out
}
