// Extracted from src/lifetimes.md:97
#![allow(unused)]
fn main() {
    fn as_str(data: &u32) -> &str {
        let s = format!("{}", data);
        &s
    }
}
