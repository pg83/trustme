// { dg-additional-options "-w" }
// { dg-output "outer\r*\ninner\r*\n" }

extern "C" {
    fn printf(s: *const i8, ...);
}

fn machin() {
    unsafe {
        let a = "outer\n\0";
        let b = a as *const str;
        let c = b as *const i8;

        printf(c, 123);
    }
}

fn bidule() {
    fn machin() {
        unsafe {
            let a = "inner\n\0";
            let b = a as *const str;
            let c = b as *const i8;

            printf(c, 123);
        }
    }

    self::machin();
    machin();
}

fn gccrs_main() -> i32 {
    bidule();

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
