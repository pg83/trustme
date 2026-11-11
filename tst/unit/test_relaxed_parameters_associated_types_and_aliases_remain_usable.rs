#![feature(trait_alias)]

trait Data {
    type Part: ?Sized;
    fn view(&self) -> &Self::Part;
}

impl Data for String {
    type Part = str;
    fn view(&self) -> &str { self.as_str() }
}

trait Printable = ?Sized + core::fmt::Debug;

fn render<T>(value: &T) -> String
where T: ?Sized + Printable,
{
    format!("{:?}", value)
}

trait SizedItem where Self: Sized {
    fn number() -> u16 { 43 }
}
impl SizedItem for u16 {}

fn main() {
    let text = String::from("ready");
    assert_eq!(Data::view(&text), "ready");
    assert_eq!(render::<str>("ok"), "\"ok\"");
    assert_eq!(render::<[u8]>(&[3, 5]), "[3, 5]");
    assert_eq!(u16::number(), 43);
}
