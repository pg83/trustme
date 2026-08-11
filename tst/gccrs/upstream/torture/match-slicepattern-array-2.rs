// { dg-output "correct\r*" }

extern "C" {
    fn puts(s: *const i8);
}

fn gccrs_main() -> i32 {
    let a = [0, 4, 5, 6, 1];
    let mut ret = 1;

    match a {
        [1, .., b] => {
            /* should not take this path */
            unsafe { puts("wrong\0" as *const str as *const i8) }
        }
        [0, .., 0] => {
            /* should not take this path */
            unsafe { puts("wrong\0" as *const str as *const i8) }
        },
        [0, .., b] => {
            ret -= b;
            unsafe { puts("correct\0" as *const str as *const i8) }
        },
        _ => {}
    }

    ret
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
