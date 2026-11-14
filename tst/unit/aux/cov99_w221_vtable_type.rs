extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn vtable_type(_: TokenStream) -> TokenStream {
    let mut writer = std::io::stdout().lock();
    for (kind, spelling) in [
        (2u8, "cov99_w221_move_seed"),
        (1u8, "::"),
        (2u8, "Ticket#vtable"),
    ] {
        writer.write_all(&[kind, spelling.len() as u8]).unwrap();
        writer.write_all(spelling.as_bytes()).unwrap();
    }
    writer.flush().unwrap();
    TokenStream::new()
}
