// A macro rule takes any of the three delimiters on both sides of `=>`. The
// matcher accepted all three, the transcriber only `(` and `{`.
#![feature(decl_macro)]

macro_rules! empty_brackets {
    [] => [];
}

macro_rules! square_body {
    ($v:expr) => [$v + 1];
}

macro chooser {
    () => [0],
    ($h:expr) => ($h),
}

fn main() {
    empty_brackets!();
    assert_eq!(square_body!(1), 2);
    assert_eq!(chooser!(), 0);
    assert_eq!(chooser!(5), 5);
}
