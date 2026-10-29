//@ aux-build: opaque_function_metadata_tags.rs

extern crate opaque_function_metadata_tags;

fn main() {
    assert_eq!(opaque_function_metadata_tags::repeated(9u16).sum::<u16>(), 18);
    assert_eq!(opaque_function_metadata_tags::callback(12)(), 13);
}
