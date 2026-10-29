#![feature(concat_idents)]
fn prefix_body_tail() -> u32 { 7 }
const CODE_VALUE: u32 = 11;
fn main() {
    assert_eq!(concat_idents!(prefix_, body, _tail)(), 7);
    assert_eq!(concat_idents!(CODE_, VALUE,), 11);
}
