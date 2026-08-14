#![feature(coroutines)]

fn main() {
    #[coroutine]
    static || {
        yield 1u8;
    };
}
