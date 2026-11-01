#![feature(decl_macro)]
#![allow(non_camel_case_types, non_upper_case_globals)]
pub macro primitive_word() {
    { use u16 as Word; let value: Word = 257; value }
}
pub mod definition {
    pub struct u16 { pub value: u32 }
    pub const u32: u32 = 131;
    pub macro record() {
        { use u16 as Word; Word { value: 127 }.value }
    }
    pub macro constant_word() {
        { use u32 as Word; let value: Word = Word; value }
    }
}
