// `(1..{ 2 })` ends the range with a block. A `{` after `..` was never the
// start of a value, because in `for _ in 0..n {}` it is the loop body -- but
// that only applies where a block may not appear, and inside parentheses it
// may.
//
// Same shape as the upstream test lint/unused/issue-90807-unused-paren.rs.
fn main() {
    let mut n = 0;
    for _ in (1..{ 3 }) {
        n += 1;
    }
    assert_eq!(n, 2);

    let r = (1..{ 4 });
    assert_eq!(r.len(), 3);

    let s = 1..{ 5 };
    assert_eq!(s.len(), 4);

    // The loop body is still the body.
    let mut m = 0;
    for _ in 0..3 { m += 1; }
    assert_eq!(m, 3);

    let limit = 2;
    let mut k = 0;
    for _ in 0..limit { k += 1; }
    assert_eq!(k, 2);
}
