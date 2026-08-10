// Extracted from src/expressions/literal-expr.md:340
#![allow(unused)]
fn main() {
    c"foo"; cr"foo";                     // foo
    c"\"foo\""; cr#""foo""#;             // "foo"
    
    c"foo #\"# bar";
    cr##"foo #"# bar"##;                 // foo #"# bar
    
    c"\x52"; c"R"; cr"R";                // R
    c"\\x52"; cr"\x52";                  // \x52
    
    c"æ";                                // LATIN SMALL LETTER AE (U+00E6)
    c"\u{00E6}";                         // LATIN SMALL LETTER AE (U+00E6)
    c"\xC3\xA6";                         // LATIN SMALL LETTER AE (U+00E6)
    
    c"\xE6".to_bytes();                  // [230]
    c"\u{00E6}".to_bytes();              // [195, 166]
}
