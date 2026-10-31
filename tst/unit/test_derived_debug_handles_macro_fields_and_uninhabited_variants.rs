#![feature(never_type)]
macro_rules! scalar { () => { u16 }; }
#[derive(Debug)]
enum Record { Live { value: scalar!(), unit: () }, Never(!) }
fn main() {
    assert_eq!(format!("{:?}", Record::Live { value: 37, unit: () }), "Live { value: 37, unit: () }");
}
