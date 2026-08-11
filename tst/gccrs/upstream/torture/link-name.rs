// { dg-additional-options "-fdump-rtl-final" }
// { dg-final { scan-rtl-dump "printf" "final" } }
// { dg-output "gcc\r*\n" }


extern "C" {
    #[link_name = "printf"]
    fn druckt(fmt: *const i8, ...);
}

fn gccrs_main() -> i32 {
    let a = "gcc\0";

    unsafe { druckt("%s\n\0" as *const str as *const i8, a as *const str as *const i8); }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
