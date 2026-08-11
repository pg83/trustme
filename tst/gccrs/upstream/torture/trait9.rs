/* { dg-output "3\r*\n" } */

extern "C" {
    fn printf(s: *const i8, ...);
}

trait FnLike<T> {
    fn call<'a>(&self, arg: &'a T) -> &'a T;
}

struct S;
impl<T> FnLike<T> for S {
    fn call<'a>(&self, arg: &'a T) -> &'a T {
        arg
    }
}

fn indirect<F: FnLike<isize>>(f: F) {
    let x = 3;
    let y = f.call(&x);

    unsafe {
        let a = "%i\n\0";
        let b = a as *const str;
        let c = b as *const i8;

        printf(c, *y);
    }
}

fn gccrs_main() -> i32 {
    indirect(S);

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
