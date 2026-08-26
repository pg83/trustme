//@ crate-type: lib
//@ compile-flags: -Znext-solver

pub fn filter_map_function_guides_range_item() {
    let mut bytes = [0; 4];
    for c in (0..0x110000).filter_map(char::from_u32) {
        let _ = c.encode_utf8(&mut bytes);
    }
}
