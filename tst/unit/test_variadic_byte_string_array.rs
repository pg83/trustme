extern "C" {
    fn printf(format: *const i8, ...);
}

fn main() {
    let bytes = b"L1\nL2\0";
    unsafe {
        let format = "%s\n\0";
        printf(format as *const str as *const i8, bytes);
    }
}
