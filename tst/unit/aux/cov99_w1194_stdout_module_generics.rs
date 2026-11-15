extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn module_args(_: TokenStream) -> TokenStream {
    std::io::stdout()
        .write_all(b"\x02\x05crate\x01\x02::\x02\x02#0\x01\x03::<\x02\x02u8\x01\x03>::\x02\x05VALUE")
        .unwrap();
    std::io::stdout().flush().unwrap();
    TokenStream::new()
}
