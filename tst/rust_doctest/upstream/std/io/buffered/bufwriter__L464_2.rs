// Extracted from library/std/src/io/buffered/bufwriter.rs:464
#![allow(unused)]
fn main() {
    use std::io::{self, BufWriter, Write};
    use std::panic::{catch_unwind, AssertUnwindSafe};

    struct PanickingWriter;
    impl Write for PanickingWriter {
      fn write(&mut self, buf: &[u8]) -> io::Result<usize> { panic!() }
      fn flush(&mut self) -> io::Result<()> { panic!() }
    }

    let mut stream = BufWriter::new(PanickingWriter);
    write!(stream, "some data").unwrap();
    let result = catch_unwind(AssertUnwindSafe(|| {
        stream.flush().unwrap()
    }));
    assert!(result.is_err());
    let (recovered_writer, buffered_data) = stream.into_parts();
    assert!(matches!(recovered_writer, PanickingWriter));
    assert_eq!(buffered_data.unwrap_err().into_inner(), b"some data");
}
