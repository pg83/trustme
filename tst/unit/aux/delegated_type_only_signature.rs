pub trait TypeOnly {
    fn echo<T>(&self, value: Option<T>) -> Option<T> { value }
}
pub struct Source;
impl TypeOnly for Source {}
