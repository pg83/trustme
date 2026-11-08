//@ aux-build: dyn_gat_metadata_arguments.rs
extern crate dyn_gat_metadata_arguments as ports;

fn main() {
    assert_eq!(ports::byte_port().read(), 43);
    assert_eq!(ports::word_port().read(), 67);
}
