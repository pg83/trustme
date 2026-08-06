// Extracted from src/tokens.md:127
#![allow(unused)]
fn main() {
    macro_rules! blackhole { ($tt:tt) => () }
    macro_rules! blackhole_lit { ($l:literal) => () }
    
    blackhole!("string"suffix); // OK
    blackhole_lit!(1suffix); // OK
}
