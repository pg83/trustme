extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn export_builtin_alias(_: TokenStream) -> TokenStream {
    "pub use ::\"#builtins\"::u8::concat as BuiltinAlias;"
        .parse()
        .unwrap()
}
