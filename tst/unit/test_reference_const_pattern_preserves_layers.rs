#[derive(PartialEq)]
struct Wrapper(*const u8);

const VALUE: &&Wrapper = &&Wrapper(core::ptr::null());

fn main() {
    let matched = match VALUE {
        VALUE => true,
        _ => false,
    };
    assert!(matched);
}
