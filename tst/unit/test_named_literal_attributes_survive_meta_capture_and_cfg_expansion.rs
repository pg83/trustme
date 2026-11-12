#![doc = "own named attribute control"]

macro_rules! named {
    ($attribute:meta) => {
        #[$attribute]
        fn value() -> u16 {
            283
        }
    };
}

named!(doc = "captured named string value");

#[cfg_attr(all(), doc = "enabled named attribute")]
fn marked() -> u16 {
    value()
}

#[cfg_attr(any(), doc = "disabled named attribute")]
fn main() {
    assert_eq!(marked(), 283);
}
