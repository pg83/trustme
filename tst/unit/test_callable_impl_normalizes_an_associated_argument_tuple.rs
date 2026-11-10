#![feature(fn_traits, unboxed_closures)]

trait Signature {
    type Args;
}

struct ArgumentSet;

impl Signature for ArgumentSet {
    type Args = (u16,);
}

struct Add(u16);

impl FnOnce<<ArgumentSet as Signature>::Args> for Add {
    type Output = u16;

    extern "rust-call" fn call_once(self, args: <ArgumentSet as Signature>::Args) -> u16 {
        self.0 + args.0
    }
}

fn main() {
    assert_eq!(Add(5)(24u16), 29);
}
