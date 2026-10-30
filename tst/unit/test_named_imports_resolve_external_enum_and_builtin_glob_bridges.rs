//@ aux-build: enum_glob_bridge_source.rs
mod bridge { pub use enum_glob_bridge_source::Signal::*; }
use bridge::{Idle, Named as Record, Value as Make};
use enum_glob_bridge_source::{platform, spelling};
fn main() {
    assert_eq!(Idle, enum_glob_bridge_source::Signal::Idle);
    assert_eq!(Make(31), enum_glob_bridge_source::Signal::Value(31));
    assert_eq!(Record { code: 37 }, enum_glob_bridge_source::Signal::Named { code: 37 });
    assert!(platform!(target_pointer_width = "64"));
    assert_eq!(spelling!(a + b), "a + b");
}
