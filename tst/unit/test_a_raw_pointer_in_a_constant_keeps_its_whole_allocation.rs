// tower-http's CORS tests have `const INITIAL_VARY_HEADERS: HeaderValue =
// HeaderValue::from_static("accept, accept-encoding")`, a `bytes::Bytes` inside:
// `ptr: bytes.as_ptr()`, `len: bytes.len()` and an `AtomicPtr` that keeps the
// constant from being shared, so each use rebuilds it from its value. The
// pointer is a `*const u8` into the whole 23-byte string, and it was rebuilt
// as a pointer to a copy of only as many bytes as a `u8` takes - the one
// byte `a` - so the header read back as `a` and whatever followed it.
use std::sync::atomic::AtomicPtr;

pub struct Bytes {
    ptr: *const u8,
    len: usize,
    data: AtomicPtr<()>,
}

impl Bytes {
    pub const fn from_static(bytes: &'static [u8]) -> Self {
        Bytes { ptr: bytes.as_ptr(), len: bytes.len(), data: AtomicPtr::new(std::ptr::null_mut()) }
    }

    pub const fn from_middle(bytes: &'static [u8], skip: usize) -> Self {
        Bytes { ptr: unsafe { bytes.as_ptr().add(skip) }, len: bytes.len() - skip, data: AtomicPtr::new(std::ptr::null_mut()) }
    }

    pub fn as_slice(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.ptr, self.len) }
    }

    pub fn before(&self, n: usize) -> u8 {
        unsafe { *self.ptr.sub(n) }
    }
}

const WHOLE: Bytes = Bytes::from_static(b"accept, accept-encoding");
const MIDDLE: Bytes = Bytes::from_middle(b"accept, accept-encoding", 8);

fn main() {
    assert_eq!(WHOLE.as_slice(), b"accept, accept-encoding");
    assert!(WHOLE.data.into_inner().is_null());
    assert_eq!(MIDDLE.as_slice(), b"accept-encoding");
    assert_eq!(MIDDLE.before(2), b',');
    assert_eq!(MIDDLE.before(8), b'a');
}
