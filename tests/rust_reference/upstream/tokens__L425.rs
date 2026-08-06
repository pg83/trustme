// Extracted from src/tokens.md:425
#![allow(unused)]
fn main() {
    c"foo"; cr"foo";                     // foo
    c"\"foo\""; cr#""foo""#;             // "foo"
    
    c"foo #\"# bar";
    cr##"foo #"# bar"##;                 // foo #"# bar
    
    c"\x52"; c"R"; cr"R";                // R
    c"\\x52"; cr"\x52";                  // \x52
}
