
#![feature(rustc_attrs)]

macro_rules! file1 {
    () => {
        "builtin_macro_include_str.rs"
    };
}

fn main () {
  include_str!(file1!()); // ok
}
