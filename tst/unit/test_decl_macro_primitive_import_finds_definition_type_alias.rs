#![feature(decl_macro)]
#![allow(non_camel_case_types)]
mod definition {
    pub type u16 = bool;
    pub macro imported_flag() {
        {
            use u16 as Flag;
            let value: Flag = true;
            value
        }
    }
}
fn main() { struct u16 { value: u32 } assert!(definition::imported_flag!()); }
