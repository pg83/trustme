// Extracted from library/core/src/option.rs:1519
#![allow(unused)]
fn main() {
    let arr_2d = [["A0", "A1"], ["B0", "B1"]];

    let item_0_1 = arr_2d.get(0).and_then(|row| row.get(1));
    assert_eq!(item_0_1, Some(&"A1"));

    let item_2_0 = arr_2d.get(2).and_then(|row| row.get(0));
    assert_eq!(item_2_0, None);
}
