// { dg-additional-options "-w" }
// { dg-output "12\r*" }

#[path = "extern_mod4/modules/mod.rs"]
mod modules;

extern "C" {
    fn printf(s: *const i8, ...);
}

fn gccrs_main() -> i32 {
    unsafe {
        let fmt_s = "%d\n\0";
        let fmt_p = fmt_s as *const str;
        let fmt_i8 = fmt_p as *const i8;

        printf(fmt_i8, modules::return_12());
    }

    return 0;
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
