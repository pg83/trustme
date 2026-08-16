// `value.match { ... }` is `match value { ... }` written as a postfix operator,
// so it chains with the other postfix forms instead of wrapping the receiver in
// parentheses. The `.` parser knew `await` and `use` but not `match`.
//
// Same shape as the upstream tests match/postfix-match/postfix-match.rs and
// match/postfix-match/pf-match-chain.rs.
#![feature(postfix_match)]

struct Bar {
    a: u32,
}

fn main() {
    let n = 1.match {
        1 => "one",
        _ => "other",
    };
    assert_eq!(n, "one");

    // Chained after a field access, and chained into another one.
    let b = Bar { a: 2 };
    let m = b.a.match {
        2 => "two",
        _ => "other",
    }
    .len();
    assert_eq!(m, 3);

    // A binding and a guard behave as they do in the prefix form.
    let v = Some(5).match {
        Some(x) if x > 4 => x * 2,
        Some(x) => x,
        None => 0,
    };
    assert_eq!(v, 10);

    // The receiver is a full postfix expression, not just an identifier.
    let s = String::from("ab");
    let l = s.len().match {
        0 => "empty",
        _ => "some",
    };
    assert_eq!(l, "some");
}
