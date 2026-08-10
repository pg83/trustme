//@ compile-fail: cannot be evaluated at compile-time
#![feature(const_precise_live_drops)]

struct CountDrop;

impl Drop for CountDrop {
    fn drop(&mut self) {}
}

const BAD: () = {
    let _value = Some(CountDrop);
};

fn main() {}
