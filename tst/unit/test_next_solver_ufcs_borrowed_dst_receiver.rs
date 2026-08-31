//@ check-pass
//@ compile-flags: -Znext-solver

use std::fmt;
use std::ops::Deref;

#[repr(transparent)]
struct ByteStr([u8]);

impl Deref for ByteStr {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl fmt::Display for ByteStr {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("bytes")
    }
}

struct ByteString(Vec<u8>);

impl ByteString {
    fn as_bytestr(&self) -> &ByteStr {
        unsafe { &*(self.0.as_slice() as *const [u8] as *const ByteStr) }
    }
}

impl fmt::Display for ByteString {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self.as_bytestr(), formatter)
    }
}

fn main() {
    let value = ByteString(vec![1]);
    let _ = format!("{value}");
}
