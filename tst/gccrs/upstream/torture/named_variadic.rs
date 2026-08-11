// { dg-output "Named variadic" }


extern "C" {
    fn printf(fmt: *const i8, variadic: ...);
}

fn print(s: &str) {
    unsafe {
        printf(
            "%s" as *const str as *const i8,
            s as *const str as *const i8,
        );
    }
}

fn gccrs_main() -> i32 {
    print("Named variadic");

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
