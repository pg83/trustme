// shellexpand 3.1.2: `type XString = <Path as ToOwned>::Owned;` and
// `fn try_into_xstring(self) -> Option<XString>` in a trait, implemented with
// `-> Option<PathBuf>`. rustc's compare_impl_method normalizes the trait's
// signature after substitution, whether or not anything was substituted.
use std::path::{Path, PathBuf};

type XString = <Path as ToOwned>::Owned;

trait PathBufExt {
    fn try_into_xstring(self) -> Option<XString>;
}

impl PathBufExt for PathBuf {
    fn try_into_xstring(self) -> Option<PathBuf> {
        Some(self)
    }
}

trait Out {
    type Owned;
}

impl Out for str {
    type Owned = String;
}

trait Ext {
    fn owned(self, suffix: <str as Out>::Owned) -> <str as Out>::Owned;
}

impl Ext for String {
    fn owned(self, suffix: String) -> String {
        self + &suffix
    }
}

fn main() {
    assert_eq!(PathBuf::from("/a").try_into_xstring(), Some(PathBuf::from("/a")));
    assert_eq!(String::from("a").owned(String::from("b")), "ab");
}
