/* serde_json's `impl PartialEq<Value> for usize`, for the units on extern crate loading. */
pub struct Value;

impl PartialEq<Value> for usize {
    fn eq(&self, _: &Value) -> bool {
        false
    }
}
