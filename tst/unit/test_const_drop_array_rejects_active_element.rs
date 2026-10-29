//@ compile-fail: cannot be evaluated at compile-time
#![feature(const_precise_live_drops)]

struct Token(u8);

impl Drop for Token {
    fn drop(&mut self) {}
}

struct Slots([Option<Token>; 3]);

const BAD: () = {
    let _slots = Slots([None, Some(Token(7)), None]);
};

fn main() {}
