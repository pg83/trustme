//@ run-pass
// A promoted borrow is storage the compiler makes inside a body and names
// after the crate the body is written in. Where the body is generic and only
// another crate instantiates it, that crate reads the storage by the defining
// crate's name -- so the defining crate has to emit it even though nothing
// there reads it. These two reach error paths in generic `std` functions that
// `std` itself never instantiates this way.

use std::io::{Cursor, Seek, SeekFrom};

fn main() {
    let mut cursor = Cursor::new(Vec::<u8>::new());
    assert!(cursor.seek(SeekFrom::End(-1)).is_err());
    assert!(std::path::absolute("").is_err());
}
