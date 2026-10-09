// A deref coercion step that is still an open variable is unified with the
// target by rustc (`coerce_borrowed_pointer` -> `unify_raw`), but rustc
// checks in order: by then an earlier expression has produced it. Here the
// expression may still be pending - `AssertUnwindSafe(&mut num)`'s argument
// coercion into the constructor's `T` (ordered_float), the `format!` call
// under `&&format!(..)` (version_check) - and binding the step froze the
// wrong type: `T = Wrapped<_>`, the call's result `= str`. Such a step now
// waits for its producer, as the first pointee already did.
use std::panic::{self, AssertUnwindSafe};

#[derive(Clone, Copy, Debug, PartialEq)]
struct Wrapped<T>(T);

fn bump(w: &mut Wrapped<u32>) {
    w.0 += 1;
}

fn version_and_date(s: &str) -> (Option<String>, Option<String>) {
    let mut components = s.lines().last().unwrap_or(s).trim().split(" ");
    let version = components.nth(1);
    let date = components.next().map(|s| s.trim_matches(|c| c == '(' || c == ')'));
    (version.map(|s| s.to_string()), date.map(|s| s.to_string()))
}

macro_rules! check_parse {
    (@ $f:expr, $s:expr => $v:expr, $d:expr) => {{
        if let (Some(v), d) = $f(&$s) {
            let e_d: Option<&str> = $d.into();
            assert_eq!((v, d), ($v.to_string(), e_d.map(|s| s.into())));
        } else {
            panic!();
        }
    }};
    ($f:expr, $s:expr => $v:expr, $d:expr) => {{
        let warn = "warning: something";
        check_parse!(@ $f, $s => $v, $d);
        check_parse!(@ $f, &format!("{}\n{}", warn, $s) => $v, $d);
    }};
}

fn read_static(minor: usize) -> String {
    format!("rustc 1.{}.0 (2015-05-13)", minor)
}

fn main() {
    let catch_op = |mut num, op: fn(&mut Wrapped<_>)| {
        let mut num_ref = AssertUnwindSafe(&mut num);
        let _ = panic::catch_unwind(move || op(&mut num_ref));
        num
    };
    assert_eq!(catch_op(Wrapped(1u32), bump), Wrapped(2));

    for v in 0..2 {
        let (version, date) = (&format!("1.{}.0", v), Some("2015-05-13"));
        check_parse!(version_and_date, read_static(v) => version, date);
    }
}
