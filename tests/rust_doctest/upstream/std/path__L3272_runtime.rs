// Extracted from library/std/src/path.rs:3272
#![allow(unused)]
fn main() {
    See Also
    
    This is a convenience function that coerces errors to false. If you want to
    check errors, call [`fs::symlink_metadata`] and handle its [`Result`]. Then call
    [`fs::Metadata::is_symlink`] if it was [`Ok`].
    
    
    
    
    
    
    Converts a [`Box<Path>`](Box) into a [`PathBuf`] without copying or
    allocating.
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    Helper struct for safely printing paths with [`format!`] and `{}`.
    
    A [`Path`] might contain non-Unicode data. This `struct` implements the
    [`Display`] trait in a way that mitigates that. It is created by the
    [`display`](Path::display) method on [`Path`]. This may perform lossy
    conversion, depending on the platform. If you would like an implementation
    which escapes the path please use [`Debug`] instead.
    
    Examples
}
