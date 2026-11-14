//@ compile-fail: Expected token

macro_rules! require_path_marker {
    ($visibility:vis :: marker) => {};
}

require_path_marker!(crate::marker);

fn main() {}
