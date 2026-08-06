// Extracted from library/core/src/any.rs:883
#![allow(unused)]
fn main() {
    use std::any::type_name_of_val;
    
    let s = "foo";
    let x: i32 = 1;
    let y: f32 = 1.0;
    
    assert!(type_name_of_val(&s).contains("str"));
    assert!(type_name_of_val(&x).contains("i32"));
    assert!(type_name_of_val(&y).contains("f32"));
}
