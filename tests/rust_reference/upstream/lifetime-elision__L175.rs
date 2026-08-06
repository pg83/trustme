// Extracted from src/lifetime-elision.md:175
#![allow(unused)]
fn main() {
    // STRING: &'static str
    const STRING: &str = "bitstring";
    
    struct BitsNStrings<'a> {
        mybits: [u32; 2],
        mystring: &'a str,
    }
    
    // BITS_N_STRINGS: BitsNStrings<'static>
    const BITS_N_STRINGS: BitsNStrings<'_> = BitsNStrings {
        mybits: [1, 2],
        mystring: STRING,
    };
}
