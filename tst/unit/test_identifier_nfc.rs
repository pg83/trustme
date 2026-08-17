// Identifiers are normalised, so a name written with a precomposed character and
// one written with a combining mark are the same name.
#![allow(non_snake_case, dead_code)]

struct Résumé(u32); // precomposed U+00E9

fn main() {
    let value = Résumé(7); // decomposed: `e` plus U+0301
    assert_eq!(value.0, 7);

    // U+304C, then U+304B followed by U+3099.
    let が = 3;
    assert_eq!(が, 3);
}
