extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn local_context(_: TokenStream) -> TokenStream {
    "fn generated_value() -> u16 {
        let local = 37u16;
        macro read($extra:expr) { local + $extra }
        read!(5)
    }".parse().unwrap()
}
