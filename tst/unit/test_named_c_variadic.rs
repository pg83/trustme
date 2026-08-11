extern "C" {
    fn named(format: *const i8, arguments: ...);
    fn ignored(format: *const i8, _: ...);
}

fn main() {
    let _ = named as unsafe extern "C" fn(*const i8, ...);
    let _ = ignored as unsafe extern "C" fn(*const i8, ...);
}
