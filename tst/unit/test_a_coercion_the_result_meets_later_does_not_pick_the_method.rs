// parquet's `TreeBuilder::reader_tree`: `field.get_fields()[0].clone()` on an
// `Arc<Type>` of a `Type: Clone` is `Arc::clone`, found at the first autoderef
// step through autoref. The result later meets `&repeated_field` passed as
// `&Type`, and that deferred coercion's destination was taken for the
// expected type of the call: `Arc::clone` was dropped for not returning a
// `Type`, `<Type as Clone>::clone` one step further down was picked, and the
// recursive `self.reader_tree(repeated_field)` had no applicable method.
// Upstream picks a method by the receiver's autoderef steps alone; what the
// result is expected to be only guides the picked method's inference.
use std::sync::Arc;

#[derive(Clone, Debug)]
pub struct Type {
    fields: Vec<Arc<Type>>,
}

impl Type {
    pub fn get_fields(&self) -> &[Arc<Type>] {
        &self.fields
    }
}

pub struct TreeBuilder;

impl TreeBuilder {
    fn is_element_type(repeated_type: &Type) -> bool {
        repeated_type.fields.is_empty()
    }

    fn reader_tree(&self, field: Arc<Type>) -> u32 {
        if field.get_fields().is_empty() {
            return 0;
        }
        let repeated_field = field.get_fields()[0].clone();
        if TreeBuilder::is_element_type(&repeated_field) {
            self.reader_tree(repeated_field) + 1
        } else {
            7
        }
    }
}

fn main() {
    let leaf = Arc::new(Type { fields: vec![] });
    let list = Arc::new(Type { fields: vec![leaf] });
    assert_eq!(TreeBuilder.reader_tree(list), 1);
}
