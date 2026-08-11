// { dg-output "Value is 10\r*\n" }


const BAZ: i32 = 10;

extern "C" {
    fn printf(s: *const i8, ...);
}

fn foo() {
    fn bar() {
        let e = BAZ;
        unsafe {
            printf("Value is %i\n" as *const str as *const i8, e);
        }
    }

    bar();
}

fn gccrs_main() -> i32 {
    foo();
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
