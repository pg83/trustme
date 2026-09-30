// axum's `insert_url_params` has `let current_params = extensions.get_mut();`
// with `get_mut<T: Send + Sync + 'static>(&mut self) -> Option<&mut T>`, and
// only `if let Some(UrlParams::InvalidUtf8InPathParam { .. }) = current_params`
// says what `T` is. Upstream `peel_off_references` takes the `&mut` off the
// expected type and stops at `?T`, which is not a reference; a variant pattern
// peels (`AdjustMode::Peel`), so its type `UrlParams` is equated with what is
// left. The pattern's own type has no references to peel, and we took the
// peeled count off it too: nothing was left to equate, and `T` stayed unknown.
use std::any::Any;

fn get_mut<T: Send + 'static>(slot: &mut Option<Box<dyn Any + Send>>) -> Option<&mut T> {
    slot.as_mut().and_then(|b| b.downcast_mut())
}

enum UrlParams {
    Params(u32),
    Invalid { key: u32 },
}

fn invalid_key(slot: &mut Option<Box<dyn Any + Send>>) -> Option<u32> {
    let current = get_mut(slot);
    if let Some(UrlParams::Invalid { key }) = current {
        return Some(*key);
    }
    None
}

fn params(slot: &mut Option<Box<dyn Any + Send>>) -> u32 {
    let current = get_mut(slot);
    if let Some(UrlParams::Params(n)) = current {
        *n += 1;
        return *n;
    }
    0
}

fn main() {
    let mut s: Option<Box<dyn Any + Send>> = Some(Box::new(UrlParams::Invalid { key: 7 }));
    assert_eq!(invalid_key(&mut s), Some(7));
    assert_eq!(params(&mut s), 0);
    let mut s: Option<Box<dyn Any + Send>> = Some(Box::new(UrlParams::Params(1)));
    assert_eq!(invalid_key(&mut s), None);
    assert_eq!(params(&mut s), 2);
    assert_eq!(params(&mut s), 3);
}
