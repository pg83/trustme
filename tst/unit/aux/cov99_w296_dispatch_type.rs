extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn dispatch_type(_: TokenStream) -> TokenStream {
    let mut out = std::io::stdout();
    for (tag, text) in [(2u8, "cov99_w296_dispatch_seed"), (1u8, "::"), (2u8, "Packet#vtable")] {
        out.write_all(&[tag, text.len() as u8]).unwrap();
        out.write_all(text.as_bytes()).unwrap();
    }
    out.flush().unwrap();
    TokenStream::new()
}
