//@ edition: 2024
//@ input: aux/line_continuation_values.bin
#![feature(gen_blocks)]

#[path = "aux/line_continuation_values.bin"]
mod values;

macro_rules! check_generator {
    ($initializer:expr) => {
        assert!({
            let mut stream = $initializer else {
                unreachable!()
            };
            stream.next() == Some(1123u16) && stream.next().is_none()
        });
    };
}

fn main() {
    assert_eq!(values::LF, "leftright");
    assert_eq!(values::CRLF, "leftright");
    assert_eq!(values::BYTE, b"leftright");
    assert_eq!(values::C.to_bytes(), b"leftright");
    check_generator!((gen {
        yield 1123u16;
    }));
}
