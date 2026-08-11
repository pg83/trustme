
#![feature(lang_items)]
pub enum Option<T> {
    Some(T),
    None,
}

pub use Option::{None, Some};

impl<T> Option<T> {
    pub fn map<R, F: FnOnce(T) -> R>(self, f: F) -> Option<R> {
        match self {
            Some(value) => Some(f(value)),
            None => None,
        }
    }
}
