// An attribute macro that hands the annotated item back unchanged.
extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn passthrough(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    item
}

// The same, after checking that each `type Name` in an `extern` block ends at
// its `;`, as a foreign type is spelled.
#[proc_macro_attribute]
pub fn foreign_types_end_at_their_name(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    use proc_macro::TokenTree;
    for token in item.clone() {
        let TokenTree::Group(block) = token else { continue };
        let tokens: Vec<TokenTree> = block.stream().into_iter().collect();
        for window in tokens.windows(3) {
            if matches!(&window[0], TokenTree::Ident(word) if word.to_string() == "type") {
                assert!(
                    matches!(&window[2], TokenTree::Punct(p) if p.as_char() == ';'),
                    "a foreign type is spelled `type {} {} ..`",
                    window[1],
                    window[2]
                );
            }
        }
    }
    item
}

// The same, after checking that the item's tokens hold no `@`: no binding in
// it has a subpattern.
#[proc_macro_attribute]
pub fn no_subpatterns(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    use proc_macro::TokenTree;
    fn walk(stream: TokenStream) {
        for token in stream {
            match token {
                TokenTree::Punct(p) => assert!(p.as_char() != '@', "a binding has a subpattern"),
                TokenTree::Group(group) => walk(group.stream()),
                _ => {}
            }
        }
    }
    walk(item.clone());
    item
}
