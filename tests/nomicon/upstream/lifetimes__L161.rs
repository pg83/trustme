// Extracted from src/lifetimes.md:161
#![allow(unused)]
fn main() {
    fn to_string(data: &u32) -> String {
        format!("{}", data)
    }
}
