#![allow(non_camel_case_types)]
macro_rules! check {
    () => {
        use u16 as Local;
        let Local { value } = Local { value: 109 };
        assert_eq!(value, 109);
    };
}
fn main() { struct u16 { value: u32 } check!(); }
