// `{:x?}` is `Debug` with the integers shown in hexadecimal. The specifier was
// recognised but not stepped over, so `?}` was copied into the output as text,
// and the hex flag itself was dropped when nothing else in the specifier
// differed from the default.
//
// Same shape as the library test coretests/fmt/num.rs
// (`test_format_debug_hex`).
fn main() {
    assert_eq!(format!("{:x?}", [70u8, 111, 111, 0]), "[46, 6f, 6f, 0]");
    assert_eq!(format!("{:X?}", [70u8, 111, 111, 0]), "[46, 6F, 6F, 0]");

    // With a width and zero padding, where the specifier was already reached
    // through the formatted path.
    assert_eq!(format!("{:02x?}", [70u8, 111, 111, 0]), "[46, 6f, 6f, 00]");
    assert_eq!(format!("{:02X?}", [70u8, 255]), "[46, FF]");

    // On a plain integer, and next to an ordinary fragment.
    assert_eq!(format!("{:x?}", 255u32), "ff");
    assert_eq!(format!("{:X?}", 255u32), "FF");
    assert_eq!(format!("{} {:x?} {}", 1, 255u32, 2), "1 ff 2");

    // The plain `Debug` form is unchanged.
    assert_eq!(format!("{:?}", [70u8, 111]), "[70, 111]");
}
