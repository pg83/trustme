//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

macro_rules! members {
    () => {
        #[edits::passthrough]
        fn middle(&self) -> u16 { 127 }
        const MIDDLE: u16 = 131;
    };
}

trait Values {
    fn before(&self) -> u16 { 137 }
    members!();
    fn after(&self) -> u16 { 139 }
}

struct Value;
impl Values for Value {}

fn main() {
    assert_eq!(Value.before() + Value.middle() + Value.after(), 403);
    assert_eq!(Value::MIDDLE, 131);
}
