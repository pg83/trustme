//@ compile-fail: tail calls can only be performed with function definitions or pointers
#![feature(explicit_tail_calls, fn_traits, unboxed_closures)]
#![allow(incomplete_features)]
struct Token(u32);
struct Relay<F>(F);
impl<F: FnOnce(u32) -> Token> Relay<F> {
    extern "rust-call" fn run(self, args: (u32,)) -> Token {
        become <F as FnOnce<(u32,)>>::call_once(self.0, args)
    }
}
fn main() { let _value = Relay(Token).run((37,)); }
