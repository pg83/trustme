// The message of a failed `assert!` is `pprust::expr_to_string` of the
// condition: parentheses the source wrote are kept and none are added,
// literals keep their spelling, a macro call prints its tokens, and a
// long condition is broken by the pretty printer at 78 columns. bytes
// checks `cnt <= self.limit` this way with `#[should_panic(expected)]`.
#![allow(unused_parens)]
use std::panic;

struct Limit {
    limit: usize,
}

impl Limit {
    fn advance(&self, cnt: usize) {
        assert!(cnt <= self.limit);
    }
}

fn message(f: impl FnOnce() + panic::UnwindSafe) -> String {
    match panic::catch_unwind(f) {
        Err(e) => {
            if let Some(s) = e.downcast_ref::<&str>() {
                s.to_string()
            } else if let Some(s) = e.downcast_ref::<String>() {
                s.clone()
            } else {
                String::new()
            }
        }
        Ok(()) => String::new(),
    }
}

fn aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa(_: i32) -> bool {
    true
}

fn bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb(_: i32) -> bool {
    true
}

fn ccccccccccccccccccccccc(_: i32, _: i32) -> bool {
    false
}

fn main() {
    panic::set_hook(Box::new(|_| {}));
    let x = 3i32;
    let y = 4i32;
    let o: Option<u8> = None;
    let v = vec![1u8];
    let limit = Limit { limit: 0 };
    let got = [
        message(|| limit.advance(1)),
        message(|| assert!((x + y) * 2 == 0)),
        message(|| assert!(((x)) == 0)),
        message(|| assert!(!v.is_empty() && x < 0)),
        message(|| assert!(matches!(o,Some(1..=5)))),
        message(|| assert!(v[0] == b'x' && '\n' == 'c' && 0x1F_u8 == 2)),
        message(|| assert!((x as i64) < 1)),
        message(|| assert!(Vec::<u8>::new().len() == 1)),
        message(|| assert!(v.iter().all(|e| *e > 5))),
        message(|| assert!(match o { Some(e) if e > 3 => true, _ => false })),
        message(|| assert!(aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa(x) && bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb(y) && ccccccccccccccccccccccc(x, y))),
    ];
    let _ = panic::take_hook();
    let expected = [
        "assertion failed: cnt <= self.limit",
        "assertion failed: (x + y) * 2 == 0",
        "assertion failed: ((x)) == 0",
        "assertion failed: !v.is_empty() && x < 0",
        "assertion failed: matches!(o,Some(1..=5))",
        r"assertion failed: v[0] == b'x' && '\n' == 'c' && 0x1F_u8 == 2",
        "assertion failed: (x as i64) < 1",
        "assertion failed: Vec::<u8>::new().len() == 1",
        "assertion failed: v.iter().all(|e| *e > 5)",
        "assertion failed: match o { Some(e) if e > 3 => true, _ => false, }",
        "assertion failed: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa(x) && bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb(y)\n    && ccccccccccccccccccccccc(x, y)",
    ];
    for (got, expected) in got.iter().zip(expected.iter()) {
        assert_eq!(got, expected);
    }
}
