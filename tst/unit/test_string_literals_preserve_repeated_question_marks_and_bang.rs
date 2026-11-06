#[inline(never)]
fn inspect(text: &str) -> u8 {
    let bytes = text.as_bytes();
    assert_eq!(bytes.len(), 9);
    assert_eq!(bytes[0], 63);
    assert_eq!(bytes[1], 63);
    assert_eq!(bytes[2], 33);
    assert_eq!(bytes[3], 63);
    assert_eq!(bytes[4], 63);
    assert_eq!(bytes[5], 33);
    assert_eq!(bytes[6], 0);
    assert_eq!(bytes[7], 90);
    assert_eq!(bytes[8], 63);
    match text {
        "??!??!\0Z?" => 27,
        _ => 0,
    }
}

fn main() {
    assert_eq!(inspect("??!??!\0Z?"), 27);
}
