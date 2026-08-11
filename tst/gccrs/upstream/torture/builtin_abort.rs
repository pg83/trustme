// { dg-shouldfail "abort should stop the program" }

#![feature(rustc_attrs)]
#![feature(intrinsics)]

mod intrinsics {
    extern "rust-intrinsic" {
        pub fn abort() -> !;
    }
}

pub fn gccrs_main () -> i32 {
    intrinsics::abort();
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
