// Whitespace the lexer must skip, an underscore inside a `\u` escape, and a
// `#[linkage]` a target implements by keeping one definition or keeping it
// local to its object.
#![feature(linkage)]

fn main() {
    // vertical tab below
    
    assert_eq!('\u{10__FFFF}', '\u{10FFFF}');
    assert_eq!("\u{10_F0FF__}x", "\u{10F0FF}x");
}

// A `#[linkage]` a target implements by keeping one definition, or by keeping
// the definition local to its object.

#[linkage = "internal"]
pub static A: bool = true;

#[linkage = "linkonce"]
pub static B: bool = true;

#[linkage = "linkonce_odr"]
pub static C: bool = true;

#[linkage = "weak_odr"]
pub static D: bool = true;
