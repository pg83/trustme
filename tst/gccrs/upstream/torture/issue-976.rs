/* { dg-output "hi\r*" } */

fn gccrs_main() -> i32 {
    {
        extern "C" {
            fn puts(s: *const i8);
        }

        unsafe {
            puts("hi\0" as *const str as *const i8);
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
