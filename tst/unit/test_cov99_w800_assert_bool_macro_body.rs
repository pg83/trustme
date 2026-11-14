//@ edition: 2021

macro_rules! panic {
    ($message:tt, $left:tt, $right:tt) => {{
        macro_rules! bool_pair {
            () => { ($left, $right) };
        }
        let _: (bool, bool) = bool_pair!();
    }};
}

fn main() {
    assert!(true, "{} {}", true, false);
}
