// Macro hosts that hand their input back unchanged, so a unit can pin down what
// the compiler wrote into a macro's token stream rather than what the macro did
// with it.
extern crate proc_macro;

use proc_macro::TokenStream;

// Function-like: the invocation body comes back as the expansion.
#[proc_macro]
pub fn echo(input: TokenStream) -> TokenStream {
    input
}

// Attribute: the annotated item comes back unchanged.
#[proc_macro_attribute]
pub fn echo_item(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    item
}

// The same, but first checking how the item was spelled: the attribute takes a
// string literal that the item's tokens have to contain.
#[proc_macro_attribute]
pub fn echo_item_spelled(attribute: TokenStream, item: TokenStream) -> TokenStream {
    let quoted = attribute.to_string();
    let expected = quoted.trim().trim_matches('"');
    let text = item.to_string();
    assert!(
        text.contains(expected),
        "the item's tokens should contain `{}`, they are: {}",
        expected,
        text
    );
    item
}
