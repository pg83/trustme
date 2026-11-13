#![feature(const_async_blocks)]

const fn prepare() -> usize {
    core::mem::forget(async { 19u8 });
    29
}

const VALUE: usize = prepare();

fn main() {
    assert_eq!(VALUE, 29);
}
