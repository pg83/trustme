// Extracted from library/core/src/array/mod.rs:524
#![allow(unused)]
fn main() {
    let x = [1, 2, 3];
    let y = x.map(|v| v + 1);
    assert_eq!(y, [2, 3, 4]);
    
    let x = [1, 2, 3];
    let mut temp = 0;
    let y = x.map(|v| { temp += 1; v * temp });
    assert_eq!(y, [1, 4, 9]);
    
    let x = ["Ferris", "Bueller's", "Day", "Off"];
    let y = x.map(|v| v.len());
    assert_eq!(y, [6, 9, 3, 3]);
}
