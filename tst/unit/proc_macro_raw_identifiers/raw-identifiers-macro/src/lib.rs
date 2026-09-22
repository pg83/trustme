// sqlparser's `Expr::Position { expr, r#in }` under `#[recursive::recursive]`:
// the attribute macro hands the function to syn, which rejects a keyword
// where a field name or binding goes. A raw identifier reaches a macro as a
// raw identifier (`Ident::new_raw`, printed `r#in`).
extern crate proc_macro;

use proc_macro::{TokenStream, TokenTree};

fn count_in(stream: TokenStream, raw: &mut usize, bare: &mut usize) {
    for tree in stream {
        match tree {
            TokenTree::Ident(ident) => match ident.to_string().as_str() {
                "r#in" => *raw += 1,
                "in" => *bare += 1,
                _ => {}
            },
            TokenTree::Group(group) => count_in(group.stream(), raw, bare),
            _ => {}
        }
    }
}

#[proc_macro_attribute]
pub fn raw_idents(_args: TokenStream, item: TokenStream) -> TokenStream {
    let (mut raw, mut bare) = (0, 0);
    count_in(item.clone(), &mut raw, &mut bare);
    let mut out: TokenStream = format!("const RAW: usize = {raw}; const BARE: usize = {bare};").parse().unwrap();
    out.extend(item);
    out
}
