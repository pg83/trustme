/* clap_builder's `Values { iter: values.map(unwrap_downcast_into), len }` where the
   field is `Map<Flatten<vec::IntoIter<Vec<AnyValue>>>, fn(AnyValue) -> T>`: the field's
   type is the expectation of the method call (`check_expr_struct_fields`), so `map`'s
   `F` is the fn pointer and the fn item reifies into it. */
use std::any::Any;
use std::iter::{Flatten, Map};
use std::sync::Arc;
use std::vec;

#[derive(Clone)]
struct AnyValue(Arc<dyn Any + Send + Sync>);
impl AnyValue {
    fn downcast_into<T: Any + Clone>(self) -> Result<T, Self> {
        match self.0.downcast_ref::<T>() {
            Some(v) => Ok(v.clone()),
            None => Err(self),
        }
    }
}

fn unwrap_downcast_into<T: Any + Clone + Send + Sync + 'static>(value: AnyValue) -> T {
    value.downcast_into().ok().expect("internal")
}

struct MatchedArg {
    vals: Vec<Vec<AnyValue>>,
}
impl MatchedArg {
    fn num_vals(&self) -> usize {
        self.vals.iter().map(|v| v.len()).sum()
    }
    fn into_vals_flatten(self) -> Flatten<vec::IntoIter<Vec<AnyValue>>> {
        self.vals.into_iter().flatten()
    }
}

pub struct Values<T> {
    iter: Map<Flatten<vec::IntoIter<Vec<AnyValue>>>, fn(AnyValue) -> T>,
    len: usize,
}

fn try_remove_many<T: Any + Clone + Send + Sync + 'static>(arg: Option<MatchedArg>) -> Result<Option<Values<T>>, ()> {
    let arg = match arg {
        Some(arg) => arg,
        None => return Ok(None),
    };
    let len = arg.num_vals();
    let values = arg.into_vals_flatten();
    let values = Values {
        iter: values.map(unwrap_downcast_into),
        len,
    };
    Ok(Some(values))
}

fn main() {
    let arg = MatchedArg { vals: vec![vec![AnyValue(Arc::new(1u64))], vec![AnyValue(Arc::new(2u64))]] };
    let v: Values<u64> = try_remove_many(Some(arg)).unwrap().unwrap();
    assert_eq!(v.len, 2);
    assert_eq!(v.iter.sum::<u64>(), 3);
}
