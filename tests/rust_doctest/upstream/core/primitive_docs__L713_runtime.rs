// Extracted from library/core/src/primitive_docs.rs:713
#![allow(unused)]
fn main() {
    fn move_away(_: String) { /* Do interesting things. */ }
    
    let [john, roa] = ["John".to_string(), "Roa".to_string()];
    move_away(john);
    move_away(roa);
}
