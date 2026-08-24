// `0.0 ..= 1.0` is a range. After a float the lexer looked for a `.` to build a
// chain of tuple indices (`x.0 .1`), and took the first `.` of the range
// operator for one -- but only when a space separated them, since the unspaced
// form was already special-cased.
//
// Same shape as the upstream test parser/issues/issue-7222.rs.
#![allow(unused)]

const FOO: f64 = 10.0;

fn classify(v: f64) -> &'static str {
    match v {
        0.0 ..= FOO => "in",
        _ => "out",
    }
}

struct Nested((u32, u32));

macro_rules! float_then_tuple_index {
    ($float:literal . $index:literal) => {};
}

fn main() {
    assert_eq!(classify(0.0), "in");
    assert_eq!(classify(10.0), "in");
    assert_eq!(classify(10.5), "out");

    // Every spacing of every range operator.
    assert!(matches!(0.5f64, 0.0..=1.0));
    assert!(matches!(0.5f64, 0.0 ..= 1.0));
    assert!(matches!(0.5f64, 0.0 ..=1.0));
    assert!(match 0.5f64 {
        0.0.. => true,
        _ => false,
    });
    assert!(match 0.5f64 {
        0.0 .. => true,
        _ => false,
    });
    assert!(match 0.5f64 {
        ..1.0 => true,
        _ => false,
    });

    // A tuple-index chain after a float-shaped token still reads as one.
    let n = Nested((7, 8));
    assert_eq!(n.0 .0, 7);
    assert_eq!(n.0 .1, 8);
    assert_eq!(n.0.0, 7);

    float_then_tuple_index!(1.34.0);
}
