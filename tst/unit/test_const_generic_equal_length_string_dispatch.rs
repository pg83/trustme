#![feature(adt_const_params, unsized_const_params)]
struct Text<const VALUE: &'static str>;
trait Code { fn code() -> u8; }
impl Code for Text<"aa"> { fn code() -> u8 { 3 } }
impl Code for Text<"bb"> { fn code() -> u8 { 7 } }
impl Code for Text<"cc"> { fn code() -> u8 { 11 } }
fn main() {
    assert_eq!(Text::<"aa">::code(), 3);
    assert_eq!(Text::<"bb">::code(), 7);
    assert_eq!(Text::<"cc">::code(), 11);
}
