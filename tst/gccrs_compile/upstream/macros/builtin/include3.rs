
#![feature(rustc_attrs)]

macro_rules! my_file {
    () => {"include_rs"};
}


include!(my_file!());

fn main() -> i32 {
    b();

    0
}
