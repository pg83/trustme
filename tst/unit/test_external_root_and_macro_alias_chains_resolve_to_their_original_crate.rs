//@ aux-build: canonical_path_source.rs
//@ aux-build: canonical_path_bridge.rs
//@ compile-variant: -O
#[macro_use]
extern crate canonical_path_bridge;

use canonical_path_bridge::{dependency as declared, origin as twice};
use declared::root::source::inner::read as first;
use twice::front::inner::{Number, read as second};

fn main() {
    let number = Number(73);
    assert_eq!(first(&number), 73);
    assert_eq!(second(&make_number!(79)), 79);
    assert_eq!(join_words!("ca", "non", "ical"), "canonical");
    {
        use canonical_path_bridge::origin::root::visible::*;
        assert_eq!(read(&Number(83)), 83);
    }
}
