//@ run-pass
/* cc 1.2.37 `Build::which`: `find_exe_in_path(&self.getenv("PATH")?)` with `getenv` returning
   `Option<Arc<OsStr>>` and the closure taking `&OsStr`.  Upstream coerces a value once its
   expression is checked, so the `?`'s `Some` arm binds the match's variable to `Arc<OsStr>`
   before the borrow is coerced into `&OsStr` - a deref coercion then.  Ordered by the node a
   rule was registered at, the borrow's coercion came first and bound the pointee to `OsStr`,
   and the arm was a mismatch. */
use std::ffi::OsStr;
use std::sync::Arc;

struct Build;

impl Build {
    fn getenv(&self, v: &str) -> Option<Arc<OsStr>> {
        Some(Arc::from(OsStr::new(v)))
    }

    fn which(&self, path_entries: Option<&OsStr>) -> Option<usize> {
        let find_exe_in_path = |path_entries: &OsStr| -> Option<usize> { Some(path_entries.len()) };
        path_entries
            .and_then(find_exe_in_path)
            .or_else(|| find_exe_in_path(&self.getenv("abc")?))
    }
}

fn main() {
    assert_eq!(Build.which(None), Some(3));
    assert_eq!(Build.which(Some(OsStr::new("xy"))), Some(2));
}
