/* { dg-output "Hello World 123\r*\n" }*/

extern "C" {
    fn printf(s: *const i8, ...);
}

fn gccrs_main() -> i32 {
    unsafe {
        let a = "Hello World %i\n";
        let b = a as *const str;
        let c = b as *const i8;

        printf(c, 123);
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
