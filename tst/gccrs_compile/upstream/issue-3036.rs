
pub trait Default: Sized {
    fn default() -> Self;
}

impl Default for () {
    fn default() -> () {
        ()
    }
}
