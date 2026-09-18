/* A pattern can be a macro call, and the name of that macro is a path: proptest's
   `prop_compose!` writes `move |$crate::proptest_helper!(@_WRAPPAT ($($var),*))| $body`,
   so the closure's parameter pattern is a call through a two-segment path.  Upstream
   never tests for `!` right after a bare identifier - `can_be_ident_pat` refuses an
   identifier followed by `!` or `::` so the name is re-read as a path, and
   `parse_pat_with_range_pat` (rustc_parse/src/parser/pat.rs) parses the whole path
   first and only then asks `qself.is_none() && self.check(exp!(Bang))`, handing the
   finished path to `parse_pat_mac_invoc` for a `PatKind::MacCall`.  So `m!(..)`,
   `crate::m!(..)` and `$crate::m!(..)` are all macro calls wherever a pattern is
   accepted, and only a qualified `<T as Tr>::m` path can never be one. */

#[macro_export]
macro_rules! wrappat {
    (@w ($a:ident)) => { $a };
    (@w ($a:ident, $b:ident)) => { ($a, $b) };
}

#[macro_export]
macro_rules! mk_closure {
    ($($arg:ident),*) => {
        move |$crate::wrappat!(@w ($($arg),*))| $($arg +)* 0
    };
}

fn take(f: impl Fn((i32, i32)) -> i32) -> i32 {
    f((3, 4))
}

fn param(wrappat!(@w (v)): i32) -> i32 {
    v * 2
}

fn main() {
    let f = move |wrappat!(@w (x))| x + 1;
    assert_eq!(f(41), 42);

    let g = move |crate::wrappat!(@w (a, b))| a * b;
    assert_eq!(g((6, 7)), 42);

    let h = mk_closure!(p, q);
    assert_eq!(take(h), 7);

    let wrappat!(@w (n)) = 10i32;
    assert_eq!(n, 10);

    let m = match 5i32 {
        wrappat!(@w (k)) => k,
    };
    assert_eq!(m, 5);

    assert_eq!(param(21), 42);
}
