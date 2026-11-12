//@ aux-build: own_named_record_value.rs
//@ compile-fail: error:

extern crate own_named_record_value;

trait Build {
    fn build() -> Self;
}

impl Build for own_named_record_value::Record {
    fn build() -> Self {
        Self
    }
}

fn main() {}
