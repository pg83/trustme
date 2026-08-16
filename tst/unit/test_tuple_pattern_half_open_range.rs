// `(.. PAT)` is a parenthesised half-open range pattern, not a tuple whose rest
// is elided. A `..` inside parentheses was always read as the rest, so the
// pattern that followed it was an unexpected token.
//
// Same shape as the upstream test half-open-range-patterns/pat-tuple-4.rs.
#![allow(unused)]

const PAT: u8 = 1;

fn classify(v: u8) -> &'static str {
    match v {
        (..PAT) => "below",
        (PAT..=3) => "middle",
        (4..) => "above",
    }
}

fn main() {
    assert_eq!(classify(0), "below");
    assert_eq!(classify(1), "middle");
    assert_eq!(classify(3), "middle");
    assert_eq!(classify(9), "above");

    // A genuine rest still parses in every position.
    let t = (1u8, 2u8, 3u8);
    let (a, ..) = t;
    assert_eq!(a, 1);
    let (.., c) = t;
    assert_eq!(c, 3);
    let (x, .., z) = t;
    assert_eq!((x, z), (1, 3));
    let (..) = t;

    // And a range next to a rest in the same pattern.
    match t {
        (..2, ..) => {}
        (2.., ..) => {}
    }
}
