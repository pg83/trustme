//@ compile-fail: Type mismatch between
trait Measure:Sized { fn value(&self)->u8; }
impl Measure for u8 { fn value(&self)->u8 { *self } }
fn sum<T:Measure>(values:[&T;2])->u8 { values[0].value()+values[1].value() }
fn main() {
    let value=17u8;
    let nested=Box::new(Box::new(23u8));
    let _=sum([&nested,&value]);
}
