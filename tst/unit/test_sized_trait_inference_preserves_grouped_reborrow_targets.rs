trait Measure:Sized { fn value(&self)->u8; }
impl Measure for u8 { fn value(&self)->u8 { *self } }
fn sum<T:Measure>(values:[&T;2])->u8 { values[0].value()+values[1].value() }
fn main() {
    let value=17u8;
    let boxed=Box::new(19u8);
    let nested=Box::new(Box::new(23u8));
    assert_eq!(sum([&value,&boxed]),36);
    let values=[&boxed,&value];
    assert_eq!(sum(values),36);
    let values=[&nested,&value];
    assert_eq!(sum(values),40);
    assert_eq!(sum::<u8>([&boxed,&value]),36);
}
