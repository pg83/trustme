// Extracted from library/std/src/io/copy.rs:38
use std::io;

fn main() -> io::Result<()> {
    let mut reader: &[u8] = b"hello";
    let mut writer: Vec<u8> = vec![];

    io::copy(&mut reader, &mut writer)?;

    assert_eq!(&b"hello"[..], &writer[..]);
    Ok(())
}
