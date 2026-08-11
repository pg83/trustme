// { dg-additional-options "-frust-compat-version=1.90 -frust-cfg=A=\"foo\"" }
// { dg-output "pass\r*\n" }
#![feature(cfg_select)]

extern "C" {
    fn printf(s: *const i8, ...);
}

fn gccrs_main() -> i32 {
    cfg_select! {
        A = "foo" => {
            unsafe {
                let a = "pass\n\0";
                printf(a as *const str as *const i8);
            }
        }
        _ => {
            unsafe {
                let a = "fail\n\0";
                printf(a as *const str as *const i8);
            }
        }
    }
    return 0;
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
