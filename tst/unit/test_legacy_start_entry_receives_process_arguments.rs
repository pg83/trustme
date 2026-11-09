#[start]
fn entry(argc: isize, argv: *const *const u8) -> isize {
    if argc > 0 && !argv.is_null() {
        0
    } else {
        9
    }
}

fn main() {
    panic!("the ordinary main must not run when a start entry is selected");
}
