/* { dg-output "3\r*\n" } */

#![feature(lang_items)]
extern "C" {
    fn printf(s: *const i8, ...);
}

trait FnLike<A, R> {
    fn call(&self, arg: A) -> R;
}

struct S;
impl<'a, T> FnLike<&'a T, &'a T> for S {
    fn call(&self, arg: &'a T) -> &'a T {
        arg
    }
}

fn indirect<F>(f: F)
where
    F: for<'a> FnLike<&'a isize, &'a isize>,
{
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
