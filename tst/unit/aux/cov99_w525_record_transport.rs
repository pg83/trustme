extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn declare(_: TokenStream) -> TokenStream {
    std::io::stdout()
        .write_all(b"\x0a\x01\x02\x03pub\x02\x06struct\x02\x03E#V\x01\x01;")
        .unwrap();
    TokenStream::new()
}

#[proc_macro]
pub fn value(_: TokenStream) -> TokenStream {
    std::io::stdout()
        .write_all(b"\x0a\x01\x02\x1bcov99_w525_collision_record\x01\x02::\x02\x03E#V")
        .unwrap();
    TokenStream::new()
}
