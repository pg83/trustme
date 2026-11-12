extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn declare_program(_: TokenStream) -> TokenStream {
    "
        fn root_source() -> u16 { 41 }
        fn main() {
            let captured = 17u16;
            macro earlier() { 0u16 }
            macro reader() { (captured, root_source()) }
            let captured = 23u16;
            let root_source = || 99u16;
            assert_eq!(reader!(), (17, 41));
            assert_eq!(captured, 23);
            assert_eq!(root_source(), 99);
        }
    "
    .parse()
    .unwrap()
}
