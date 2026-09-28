// indexmap 2.11 is `#![deny(unsafe_code)]` and marks the impl blocks that need
// it `#[allow(unsafe_code)]`; fluent-bundle, regex-automata and wasmparser build
// it. Upstream lint levels nest crate > module > impl > item > block
// (rustc_lint's `LintLevelsBuilder` pushes one level per attributed node), so
// the impl's `allow` holds for every method in it. Here an impl's lint
// attribute was not recorded at all, and each `unsafe` block in those methods
// was reported under the crate's `deny`.
#![deny(unsafe_code)]

#[repr(transparent)]
struct Slice {
    bytes: [u8],
}

#[allow(unsafe_code)]
impl Slice {
    const fn from_bytes(bytes: &[u8]) -> &Self {
        unsafe { &*(bytes as *const [u8] as *const Self) }
    }

    fn first(&self) -> u8 {
        unsafe { *self.bytes.get_unchecked(0) }
    }
}

trait Head {
    fn head(&self) -> u8;
}

#[allow(unsafe_code)]
impl Head for [u8] {
    fn head(&self) -> u8 {
        unsafe { *self.get_unchecked(0) }
    }
}

fn main() {
    let slice = Slice::from_bytes(b"ab");
    assert_eq!(slice.bytes.len(), 2);
    assert_eq!(slice.first(), b'a');
    assert_eq!(b"xy"[..].head(), b'x');
}
