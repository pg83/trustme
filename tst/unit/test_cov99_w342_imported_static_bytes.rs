//@ aux-build: cov99_w342_static_bytes.rs

const BYTE: u8 = cov99_w342_static_bytes::BYTES[2];

fn main() {
    assert_eq!(BYTE, b'4');
}
