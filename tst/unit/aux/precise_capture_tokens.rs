extern crate proc_macro;
use proc_macro::TokenStream;
#[proc_macro_attribute]
pub fn keep(expected: TokenStream, item: TokenStream) -> TokenStream {
    let expected = expected.to_string();
    let expected = expected.trim().trim_matches('"');
    let actual: String = item.to_string().chars().filter(|c| !c.is_whitespace()).collect();
    let actual = actual.replace(",>", ">").replace(",)", ")");
    assert!(actual.contains(expected), "missing {} in {}", expected, actual);
    item
}
