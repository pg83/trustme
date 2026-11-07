//@ compile-fail: error:

fn inspect<F: Fn() -> u32>(callback: F) {
    match callback() {
        true => (),
        false => (),
    }
}

fn main() {}
