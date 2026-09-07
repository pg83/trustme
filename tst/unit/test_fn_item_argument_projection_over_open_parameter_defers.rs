/* `conv(v).map(Cow::Owned)`: the constructor is a function item
   `fn(<B as ToOwned>::Owned) -> Cow<B>` with `B` still open.  Its `FnOnce<(OsString,)>`
   candidate relates `OsString` with `<?B as ToOwned>::Owned`; upstream normalizes such a
   projection first - to a fresh variable, the projection left as an obligation - so the
   relation waits, the candidate applies, and `map`'s expected output `Cow<OsStr>` fixes
   `B`.  Treating the open projection as rigid rejected the only candidate and left
   `?B: ToOwned` uninferred. */
use std::borrow::Cow;
use std::ffi::{OsStr, OsString};

fn conv(v: Vec<u8>) -> Result<OsString, ()> {
    Ok(OsString::from(String::from_utf8(v).map_err(|_| ())?))
}

fn from_raw<'a>(v: Vec<u8>) -> Result<Cow<'a, OsStr>, ()> {
    conv(v).map(Cow::Owned)
}

fn main() {
    assert_eq!(from_raw(b"ab".to_vec()).unwrap().to_str(), Some("ab"));
}
