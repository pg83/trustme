// { dg-output "l\r*\np\r*\nl\r*\np\r*\n" }
// { dg-additional-options "-w" }
#![feature(lang_items)]

extern "C" {
    fn printf(s: *const i8, ...);
}

struct ParamDroppable;
struct LocalDroppable;

impl Drop for ParamDroppable {
    fn drop(&mut self) {
        let msg = "p\n\0" as *const str as *const i8;
        unsafe {
            printf(msg);
        }
    }
}

impl Drop for LocalDroppable {
    fn drop(&mut self) {
        let msg = "l\n\0" as *const str as *const i8;
        unsafe {
            printf(msg);
        }
    }
}

fn named_param(_p: ParamDroppable) {
    let _l = LocalDroppable;
}

fn wildcard_param(_: ParamDroppable) {
    let _l = LocalDroppable;
}

fn gccrs_main() -> i32 {
    named_param(ParamDroppable);
    wildcard_param(ParamDroppable);
    0
}
fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
