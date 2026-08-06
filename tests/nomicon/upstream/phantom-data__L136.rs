// Extracted from src/phantom-data.md:136
fn main() {
    let mut v: Vec<&str> = Vec::new();
    let s: String = "Short-lived".into();
    v.push(&s);
    drop(s);
} // <- `v` is dropped here
