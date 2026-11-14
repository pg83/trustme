//@ proc-macro-aux-build: cov99_w126_joint_slash_symbols.rs

macro_rules! accept_slashes {
    (/ /) => { 126_u32 };
}

fn main() {
    assert_eq!(cov99_w126_joint_slash_symbols::adjacent_slashes!(), 126);
}
