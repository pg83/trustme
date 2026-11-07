//@ compile-fail: error:

fn inspect<F: Fn() -> u32>(callback: F) {
    match callback() {
        1u32..=3u16 => (),
        _ => (),
    }
}

fn main() {}
