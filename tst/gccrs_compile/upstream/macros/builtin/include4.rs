
#![feature(rustc_attrs)]

macro_rules! my_file {
    () => {"include_rs2"};
}
fn main() -> i32 {
    let _ = include!(my_file!());

    0
}
