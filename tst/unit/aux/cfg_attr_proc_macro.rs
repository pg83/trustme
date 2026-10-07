// derive-deftly-macros declares its macros `#[cfg_attr(proc_macro, proc_macro)]`,
// so the same source also builds as an ordinary library.
extern crate proc_macro;
use proc_macro::TokenStream;

#[cfg_attr(proc_macro, proc_macro)]
pub fn answer(_input: TokenStream) -> TokenStream {
    "42".parse().unwrap()
}
