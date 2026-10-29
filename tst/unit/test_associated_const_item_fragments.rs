macro_rules! make_trait { ($item:item) => { trait Value { $item } }; }
macro_rules! make_impl { ($item:item) => { impl Value for () { $item } }; }
make_trait!(const VALUE: u8 = 17;);
make_impl!(const VALUE: u8 = 23;);
static MODULE_VALUE: u8 = 31;
fn main() {
    assert_eq!(<() as Value>::VALUE, 23);
    assert_eq!(MODULE_VALUE, 31);
}
