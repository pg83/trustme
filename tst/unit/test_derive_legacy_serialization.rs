//@ aux-build: rustc_serialize.rs
extern crate rustc_serialize;
use rustc_serialize::{Decodable, Decoder, Encodable, Encoder};

#[derive(RustcEncodable, RustcDecodable, Debug, PartialEq)]
struct Unit;
#[derive(RustcEncodable, RustcDecodable, Debug, PartialEq)]
struct Tuple(u8, u16);
#[derive(RustcEncodable, RustcDecodable, Debug, PartialEq)]
struct Named<T> { first: u8, value: T }
#[derive(RustcEncodable, RustcDecodable, Debug, PartialEq)]
enum Choice { Unit, Tuple(u8, u16), Named { value: u8 } }
#[derive(RustcDecodable)]
enum Empty {}

struct Buffer {
    values: Vec<u64>,
    index: usize,
    fail_at: Option<usize>,
}

impl Encoder for Buffer {
    type Error = &'static str;
    fn emit_u64(&mut self, value: u64) -> Result<(), Self::Error> {
        if self.fail_at == Some(self.values.len()) {
            return Err("write failed");
        }
        self.values.push(value);
        Ok(())
    }
}

impl Decoder for Buffer {
    type Error = &'static str;
    fn read_u64(&mut self) -> Result<u64, Self::Error> {
        let value = self.values.get(self.index).copied().ok_or("eof")?;
        self.index += 1;
        Ok(value)
    }
    fn error(&mut self, _: &str) -> Self::Error { "invalid variant" }
}

fn round_trip<T: Encodable + Decodable + PartialEq + std::fmt::Debug>(value: T, expected: &[u64]) {
    let mut buffer = Buffer { values: vec![], index: 0, fail_at: None };
    value.encode(&mut buffer).unwrap();
    assert_eq!(buffer.values, expected);
    assert_eq!(T::decode(&mut buffer).unwrap(), value);
    assert_eq!(buffer.index, expected.len());
    for limit in 0..expected.len() {
        let mut failing = Buffer { values: vec![], index: 0, fail_at: Some(limit) };
        assert_eq!(value.encode(&mut failing), Err("write failed"));
        assert_eq!(failing.values, expected[..limit]);
        let mut short = Buffer { values: expected[..limit].to_vec(), index: 0, fail_at: None };
        assert_eq!(T::decode(&mut short), Err("eof"));
    }
}

fn main() {
    round_trip(Unit, &[]);
    round_trip(Tuple(7, 300), &[7, 300]);
    round_trip(Named { first: 4, value: 500u16 }, &[4, 500]);
    round_trip(Choice::Unit, &[0]);
    round_trip(Choice::Tuple(8, 900), &[1, 8, 900]);
    round_trip(Choice::Named { value: 9 }, &[2, 9]);
    let mut invalid = Buffer { values: vec![9], index: 0, fail_at: None };
    assert_eq!(Choice::decode(&mut invalid), Err("invalid variant"));
    invalid.index = 0;
    assert!(matches!(Empty::decode(&mut invalid), Err("invalid variant")));
}
