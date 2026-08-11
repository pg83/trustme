/* { dg-output "parent123\r*\nchild\r*\n" } */

#![feature(lang_items)]

extern "C" {
    fn printf(s: *const i8, ...);
}
 
struct Foo(i32);
trait Parent {
    fn parent(&self);
}

trait Child : Parent {
    fn child(&self);
}

impl Parent for Foo {
    fn parent(&self) {
        unsafe {
            let parent = "parent%i\n\0";
            let msg = parent as *const str;
            printf(msg as *const i8,self.0);
        }
    }
}

impl Child for Foo {
    fn child(&self) {
        let _ = self;
        unsafe {
            let child = "child\n\0";
            let msg = child as *const str;
            printf(msg as *const i8);
        }
    }
}

pub fn gccrs_main() -> i32 {
    let a = Foo(123);
    let b: &dyn Child = &a;

    b.parent();
    b.child();
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
