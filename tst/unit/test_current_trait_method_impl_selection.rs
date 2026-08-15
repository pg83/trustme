//@ crate-type: lib

trait Compare<Rhs> {
    fn compare(&self, rhs: &Rhs);
}

struct Value;
struct Other;

impl Compare<Value> for Value {
    fn compare(&self, _: &Value) {}
}

impl Compare<Other> for Value {
    fn compare(&self, _: &Other) {
        let value = Value;
        self.compare(&value);
    }
}
