// yansi's `conditions!`: a `$feat:meta` fragment matched against
// `feature = "detect-tty"` must end after the literal, so that the `$f:expr`
// after it gets `is_tty(&std::io::stdout())`. Upstream parses the value of
// `name = value` as one expression (`parse_attr_args`: `AttrArgs::Eq` holds
// `parse_expr_force_collect`); a run of tokens up to the next delimiter or
// comma would swallow the following fragment.
macro_rules! conditions {
    ($feat:meta $($f:expr, $CACHED:ident: $cached:ident, $LIVE:ident: $live:ident),* $(,)?) => (
        #[cfg($feat)]
        impl Condition {
            $( pub const $CACHED: u32 = 1; )*
            $( pub fn $live() -> bool { $f } )*
        }
    )
}
struct Condition;
fn is_tty(x: &u32) -> bool {
    *x == 1
}
conditions! { all()
    is_tty(&1),
        STDOUT_IS_TTY: stdout_is_tty,
        STDOUT_IS_TTY_LIVE: stdout_is_tty_live,
    is_tty(&2),
        STDERR_IS_TTY: stderr_is_tty,
        STDERR_IS_TTY_LIVE: stderr_is_tty_live,
}
macro_rules! value_of {
    ($m:meta) => { stringify!($m) };
}
fn main() {
    assert!(Condition::stdout_is_tty_live());
    assert!(!Condition::stderr_is_tty_live());
    assert_eq!(Condition::STDOUT_IS_TTY + Condition::STDERR_IS_TTY, 2);
    assert_eq!(value_of!(doc = concat!("a", "b")), "doc = concat!(\"a\", \"b\")");
}
