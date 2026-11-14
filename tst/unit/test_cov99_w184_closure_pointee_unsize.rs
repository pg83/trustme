fn invoke<T: ?Sized + 'static, F: FnOnce() -> &'static T>(f: F) -> &'static T {
    f()
}

fn main() {
    let value = invoke(|| &73u8);
    let _ = value;
}
