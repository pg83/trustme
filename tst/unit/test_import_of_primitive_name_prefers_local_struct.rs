#![allow(non_camel_case_types)]
fn main() {
    struct u8 { value: u16 }
    use u8 as Local;
    let Local { value } = Local { value: 61 };
    assert_eq!(value, 61);
}
