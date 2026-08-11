/* { dg-output "Hello World\r*" }*/

extern "C" {
    fn puts(s: *const i8);
}

fn gccrs_main() -> i32 {
    unsafe {
        let a = "Hello World";
        let b = a as *const str;
        let c = b as *const i8;

        puts(c);
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
