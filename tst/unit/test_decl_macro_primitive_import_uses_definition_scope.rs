#![feature(decl_macro)]
#![allow(non_camel_case_types)]
macro imported_word() {
    {
        use u16 as Word;
        let value: Word = 257;
        value
    }
}
fn main() {
    struct u16 { value: u32 }
    assert_eq!(imported_word!(), 257);
}
