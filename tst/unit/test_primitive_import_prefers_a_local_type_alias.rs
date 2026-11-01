#![allow(non_camel_case_types)]
fn main() {
    type u16 = bool;
    use u16 as Flag;
    let value: Flag = true;
    assert!(value);
}
