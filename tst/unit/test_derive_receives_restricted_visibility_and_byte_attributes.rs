//@ proc-macro-aux-build: inspect_restricted_byte_field.rs
extern crate inspect_restricted_byte_field;

mod outer {
    mod inner {
        #[derive(inspect_restricted_byte_field::InspectBytes)]
        #[bytes(b"\x00\xffA")]
        pub(in crate::outer) struct Packet(pub u16);
    }

    pub fn read() -> u16 { inner::Packet(42).0 }
}

fn main() { assert_eq!(outer::read(), 42); }
