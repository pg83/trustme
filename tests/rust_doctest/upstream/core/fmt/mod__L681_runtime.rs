// Extracted from library/core/src/fmt/mod.rs:681
#![allow(unused)]
fn main() {
    use std::fmt::Arguments;
    
    fn write_str(_: &str) { /* ... */ }
    
    fn write_fmt(args: &Arguments<'_>) {
        if let Some(s) = args.as_str() {
            write_str(s)
        } else {
            write_str(&args.to_string());
        }
    }
}
