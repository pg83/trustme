//@ compile-fail: Byte string pattern has length

fn inspect<F: Fn() -> &'static [u8; 3]>(callback: F) {
    match callback() {
        b"ok" => (),
        _ => (),
    }
}

fn main() {}
