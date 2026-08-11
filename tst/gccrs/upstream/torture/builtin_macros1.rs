#![feature(rustc_attrs)]

extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn gccrs_main() -> i32 {
    assert!(file!().ends_with("tst/gccrs/upstream/torture/builtin_macros1.rs"));

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
