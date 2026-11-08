//@ compile-fail: Attempting to move out of invalid slot
#![allow(dead_code)]
struct Token;
fn consume(_: Token) {}

fn mutable_late() {
    let mut flag = 0u8;
    let mut mutate;
    let mut invoke = || mutate();
    mutate = || { flag = 1; };
    invoke();
    let _ = flag;
}

fn consuming_late() {
    let take;
    let invoke = || take();
    let token = Token;
    take = move || consume(token);
    invoke();
}

fn main() {}
