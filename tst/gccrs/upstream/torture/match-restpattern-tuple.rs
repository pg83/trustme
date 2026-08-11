// { dg-output "correct\r*" }

extern "C" {
    fn puts(s: *const i8);
}

fn gccrs_main() -> i32 {
    let x = (1, 2, 3, 4);
    let mut ret = 1;

    match x {
        (1, .., 2, 4) => {
            /* should not take this path */
            unsafe { puts("wrong\0" as *const str as *const i8) }
        },
        (2, ..) => {
            /* should not take this path */
            unsafe { puts("wrong\0" as *const str as *const i8) }
        },
        (b, .., 4) => { 
            ret -= b;
            unsafe { puts("correct\0" as *const str as *const i8) }
        },
        _ => {}
    }

    ret
}
fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
