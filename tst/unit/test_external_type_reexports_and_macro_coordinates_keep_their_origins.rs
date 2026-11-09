//@ aux-build: reexported_entry_coordinates.rs

extern crate reexported_entry_coordinates;

use reexported_entry_coordinates::PublicEntry;

fn main() {
    let expected_line = line!() + 1;
    let (entry, filename, call_line) = reexported_entry_coordinates::located_entry!(53);
    let PublicEntry { value } = entry;
    assert_eq!(value, 53);
    assert_eq!(call_line, expected_line);
    assert!(filename.ends_with("test_external_type_reexports_and_macro_coordinates_keep_their_origins.rs"));

    let direct = PublicEntry { value: 59 };
    assert_eq!(direct.value, 59);
}
