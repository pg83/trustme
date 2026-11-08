//@ edition: 2024
//@ compile-fail: cannot match an inherited shared reference
#![feature(ref_pat_eat_one_layer_2024)]
fn main() {
    let mut number = 13u8;
    let values = &[&mut number];
    let [&mut value] = values;
    let _ = value;
}
