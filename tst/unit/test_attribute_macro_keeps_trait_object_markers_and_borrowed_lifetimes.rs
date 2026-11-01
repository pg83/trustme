//@ proc-macro-aux-build: preserve_bound_types.rs
use preserve_bound_types::preserve;

trait Value { fn value(&self) -> u16; }
impl Value for u16 { fn value(&self) -> u16 { *self } }

#[preserve]
fn forward<'a>(value: Box<dyn Value + Send + Sync + 'a>) -> Box<dyn Value + Send + Sync + 'a> { value }

fn require_send_sync<T: Send + Sync>(_: &T) {}

fn main() {
    let relay: for<'a> fn(Box<dyn Value + Send + Sync + 'a>) -> Box<dyn Value + Send + Sync + 'a> = forward;
    let value = relay(Box::new(41u16));
    require_send_sync(&value);
    assert_eq!(value.value(), 41);
}
