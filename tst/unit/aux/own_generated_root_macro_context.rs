extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn root_context(_: TokenStream) -> TokenStream {
    "mod generated {
        fn helper() -> u16 { 37 }
        pub macro read($extra:expr) { helper() + $extra }
    }".parse().unwrap()
}
