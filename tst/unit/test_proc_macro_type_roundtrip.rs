//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::echo;

macro_rules! identity {
    ($name:ident, $ty:ty) => {
        echo! { fn $name(value: $ty) -> $ty { value } }
    };
}

trait Value { fn value(&self) -> u8; }
impl Value for u8 { fn value(&self) -> u8 { *self } }
trait Project { type Output; }
struct Source;
impl Project for Source { type Output = u16; }

identity!(array, [u8; { 1 + 2 }]);
identity!(projected, <Source as Project>::Output);
identity!(function, for<'a> fn(&'a u8) -> &'a u8);
identity!(object, Box<dyn Value + Send + Sync + 'static>);
identity!(pointer, *const (u8, u16));
identity!(mutable_pointer, *mut [u8; 3]);
identity!(nested, (Option<Result<u8, u16>>, [(); 0]));

fn borrow(value: &u8) -> &u8 { value }

fn main() {
    assert_eq!(array([2, 4, 6]), [2, 4, 6]);
    assert_eq!(projected(300), 300);
    let value = 7;
    assert!(std::ptr::eq(function(borrow)(&value), &value));
    assert_eq!(object(Box::new(9u8)).value(), 9);
    let pair = (1, 200);
    assert_eq!(pointer(&pair), &pair as *const _);
    let mut bytes = [1, 2, 3];
    unsafe { (*mutable_pointer(&mut bytes))[1] = 8; }
    assert_eq!(bytes, [1, 8, 3]);
    assert_eq!(nested((Some(Err(500)), [])), (Some(Err(500)), []));
}
